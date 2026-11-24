现状里和持久化相关的几点

1. 有统一的 engine 接口（kvs_engine_set/get/del/mod），持久化应该挂在这一层之上，这样 hash、array 和以后的 rbtree 都能复用。
2. 服务是单线程的：proactor_start 是一个 io_uring 循环，不用考虑加锁，这让持久化简单很多。
3. 服务现在没有退出路径：proactor_start 里是 while (1)，kvs_engine_destroy() 实际上永远执行不到。所以"退出时保存"需要先加信号处理，Ctrl-C 时才能干净退出。
4. 协议层会改写输入：strtok 会修改 msg。因此启动恢复时不能把日志喂回 kvs_protocol，应该直接调用 kvs_engine_*。

---

方案一：全量持久化（快照，类似 Redis 的 RDB）

思路：把内存中所有 key/value 一次性导出到文件；启动时整个读回来。

1. 给 engine 接口加一个遍历函数（只有这一处要改到 engine 层）：
typedef int (*kvs_foreach_cb)(const char *key, const char *v
int kvs_engine_foreach(kvs_foreach_cb cb, void *arg);
hash 版本按 bucket 遍历每条链表，array 版本遍历 table，rbtre 要知道底层用的是什么数据结构。

2. 文件格式用二进制加长度前缀（不要用"空格分隔的文本"，否则value 一旦带空格或换行就解析不了）：

```
[magic "KVS1"][version u8][count u32]
  [klen u32][key bytes][vlen u32][value bytes]
[checksum u32]   // CRC32 或简单累加，加载时校验
```

3. 原子写入，避免写到一半崩溃把旧快照也毁掉：
写 dump.kvs.tmp → fflush → fsync → rename(tmp, dump.kvs)
rename 是原子操作，所以文件要么是旧的完整版本，要么是新的完整版本。

4. 触发时机（可以全做，也可以挑一部分）：
- 新增一个 SAVE 命令，手动触发
- 收到 SIGINT/SIGTERM 时，退出前保存
- 定时保存：比如每 60 秒并且有修改时才存（用一个 dirty 计数

5. 进阶版 BGSAVE：用 fork()，子进程利用写时复制（COW）拿到一 处理请求。你的 mempool 也是普通的进程内存，fork之后可以直接遍历。建议先做同步 SAVE，跑通了再加 fork。

缺点：两次快照之间崩溃，这段时间的写入会丢失。

---

方案二：增量持久化（追加日志，类似 Redis 的 AOF）

思路：每条写操作执行成功后，把它追加到日志文件末尾；启动时从头重放一遍。

1. 挂载点在 kvs_filter_protocol 里，只记录真正改变了数据的操作：
- SET 且 ret == 0（key 已存在时返回 1，不需要记录）
- DEL 且 ret == 0
- MOD 且 ret == 0
- GET 和 EXIST 不记录

更干净的做法是在 engine 外面包一层 kvs_store_set()：先执行，成功后再写日志。这样协议层不用关心持久化。

2. 记录格式，同样用长度前缀：
[op u8: SET/DEL/MOD][klen u32][key][vlen u32][value]   // DE

3. 刷盘策略（这是 AOF 的核心权衡），可以做成配置项：

┌──────────┬──────────────────────────────────┬─────────────
│   策略   │               做法               │ 崩溃时最多丢失 │ 性能 │
├──────────┼──────────────────────────────────┼─────────────
│ always   │ 每条都 fsync                     │ 0 条           │ 慢   │
├──────────┼──────────────────────────────────┼─────────────
│ everysec │ 先 write，每秒 fsync 一次        │ 约 1 秒        │ 推荐 │
├──────────┼──────────────────────────────────┼─────────────
│ no       │ 只 write，由操作系统决定何时刷盘 │ 不确定         │ 最快 │
└──────────┴──────────────────────────────────┴─────────────

在单线程循环里实现 everysec：io_uring_wait_cqe 会一直阻塞， imeout（1 秒超时），醒来后检查距离上次 fsync 是否超过 1 秒。

4. 恢复时容错：崩溃可能留下写了一半的最后一条记录。读到不完  截断到最后一条完整记录处，然后继续启动，不要直接报错退出。

5. 日志压缩（rewrite）：AOF 会无限增长，比如对同一个 key 做  rewrite 的做法是：用 foreach 把当前内存状态写成一个新的AOF（每个 key 一条 SET，正好复用方案一的遍历函数），再 rename 替换掉旧文件。触发条件可以是文件大小超过上次 rewrite 后的 2 倍。

---

启动流程

init_kvengine();          // 创建内存池和 engine
persist_load();           // 新增：开了 AOF 就重放 AOF，否则
install_signal_handler(); // 新增：SIGINT/SIGTERM 时设置退出标志
proactor_start(...);      // 循环里检查退出标志，退出前 SAVE

如果 AOF 文件存在就优先用它，因为它的数据通常更新。恢复期间 的操作会再被写回 AOF 一遍。

文件组织建议

src/persist/persist.h    // 公共接口：persist_load / persist
src/persist/snapshot.c   // 全量持久化
src/persist/aof.c        // 增量持久化 + rewrite
tests/test_persist.c     // 写入 → 保存 → 销毁 → 重新加载 → 逐条比对；再测一下截断文件的恢复
"全量"和"增量"通过运行时参数选择（比如 ./kvstore_hash 2000 -le 编出不同的二进制，和你现在按 engine 区分产物的方式一样。

建议的实现顺序

1. kvs_engine_foreach 和同步快照 SAVE，加上启动加载和测试（ 历函数）
2. 信号处理，加上退出时自动保存
3. AOF 追加和重放，加上 everysec 刷盘
4. AOF rewrite（复用第 1 步的 foreach）
5. （可选）fork 后台保存，或者"快照 + 快照之后的 AOF"的混合
