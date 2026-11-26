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

方案A. 纯AOF  怎量文件会变得特别大
方案B. 快照 + 快照之后的 AOF（推荐）

dump.kvs      上次 SAVE 时的完整数据     ← 已经完成
append.aof    上次 SAVE 之后的每一次写操作  ← 这次要做的

启动：  加载 dump.kvs → 重放 append.aof → 开始服务
SAVE：  写 dump.kvs → 成功后清空 append.aof

- 优点：日志的压缩就是 SAVE，不需要再写 rewrite。启动时只需重放最近一段日志，比方案 A 快。
- 缺点：要管理两个文件，并且要考虑"快照写完了、日志还没清空就崩溃"这种中间状态。下面会专门分析，结论是不会出错。

顺序: 先执行， 在记录， 最后回复客户端

写入的 方案 

1. always 每次 write之后都 fsync， 崩溃时最多丢失 0 条， 最简单实现
2. everysec 只write， 每秒都 fsync， 崩溃时最多丢失 1 秒的数据， 
3. 只write， 由操作系统决定什么时候刷盘， 崩溃时 不确定会丢失多少数据， linux通常是30秒左右j

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
