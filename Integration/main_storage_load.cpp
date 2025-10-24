#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>

#include "dbms/storage/storage_options.h"
#include "dbms/storage/storage_types.h"
#include "dbms/storage/segment/segment_manager.h"
#include "dbms/storage/space/free_space_manager.h"
#include "dbms/storage/buffer/buffer_pool_manager.h"
#include "dbms/storage/table/table_heap.h"
#include "dbms/storage/record/schema.h"
#include "dbms/storage/record/tuple.h"
#include "dbms/storage/table/table_iterator.h"

// 替换器
#include "internal/buffer/clock_replacer.h"
#ifdef DBMS_STORAGE_ENABLE_LRUK
#include "internal/buffer/lruk_replacer.h"
#endif

using namespace dbms::storage;

struct Args {
  std::string data_file;               // 例如 ../supplier.tbl
  std::string base_dir = "./dbdata";   // 段/页输出目录
  uint32_t    page_size = kDefaultPageSize;
  int         frames = 256;            // 缓冲帧数
  std::string replacer = "clock";      // clock | lruk
  int         log_every = 1000;        // 每 N 条打印一次统计
  int         k = 2;                   // LRU-K 的 K 值（仅 lruk 有效）
  seg_id_t    seg = 1;                 // 测试用单段
};

// --- 命令行解析（保持你原有风格：位置参数 + --key=val） ---
Args ParseArgs(int argc, char** argv) {
  Args a;
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0]
              << " <supplier.tbl> [--base_dir=./dbdata] [--frames=256]"
              << " [--page=8192] [--replacer=clock|lruk] [--k=2] [--log_every=1000]\n";
    std::exit(1);
  }
  a.data_file = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string s(argv[i]);
    auto eat = [&](const char* key, auto& out) {
      std::string k = std::string("--") + key + "=";
      if (s.rfind(k, 0) == 0) {
        std::stringstream ss(s.substr(k.size()));
        ss >> out;
        return true;
      }
      return false;
    };
    if (eat("base_dir", a.base_dir)) continue;
    if (eat("frames", a.frames)) continue;
    if (eat("page", a.page_size)) continue;
    if (eat("replacer", a.replacer)) continue;
    if (eat("log_every", a.log_every)) continue;
    if (eat("k", a.k)) continue;
  }
  return a;
}

// --- 更稳健的分割：支持无行尾 '|'，并去除行末 \r（CRLF） ---
static std::vector<std::string> SplitPipe(const std::string& raw) {
  std::vector<std::string> fields;
  fields.reserve(8);

  size_t len = raw.size();
  if (len && raw[len - 1] == '\r') --len;  // 去掉 CR

  size_t start = 0;
  while (start <= len) {
    size_t pos = raw.find('|', start);
    if (pos == std::string::npos || pos >= len) {
      fields.emplace_back(raw.substr(start, len - start)); // 把最后一段也加入
      break;
    }
    fields.emplace_back(raw.substr(start, pos - start));
    start = pos + 1;
  }

  // 若是“行尾带 |”的 TPC-H 格式，这里会有 8 段且最后一段为空，丢掉它
  if (fields.size() == 8 && fields.back().empty()) fields.pop_back();

  return fields; // 期望得到 7 段
}

// --- FSM 桶尺寸日志 ---
static void LogFsm(const FreeSpaceManager& fsm) {
  auto bins = fsm.BinSizes();
  std::ostringstream oss;
  oss << "FSM bins = [";
  for (size_t i = 0; i < bins.size(); ++i) {
    oss << bins[i];
    if (i + 1 < bins.size()) oss << ", ";
  }
  oss << "]";
  std::cout << oss.str() << "\n";
}

// --- TPC-H supplier 的 Schema（与之前一致） ---
Schema MakeSupplierSchema() {
  std::vector<Column> cols = {
    {"suppkey",   Type::INT32,   0,   false},
    {"name",      Type::CHAR,    25,  false},   // CHAR(25)
    {"address",   Type::VARCHAR, 40,  false},   // VARCHAR(40)
    {"nationkey", Type::INT32,   0,   false},
    {"phone",     Type::CHAR,    15,  false},   // CHAR(15)
    {"acctbal",   Type::DOUBLE,  0,   false},
    {"comment",   Type::VARCHAR, 101, true},
  };
  return Schema(std::move(cols), /*use_null_bitmap=*/false);
}

int main(int argc, char** argv) {
  Args args = ParseArgs(argc, argv);
  std::filesystem::create_directories(args.base_dir);

  // === 组件初始化 ===
  SegmentManager sm(args.page_size, args.base_dir);
  if (!sm.EnsureSegment(args.seg).ok()) {
    std::cerr << "EnsureSegment failed\n"; return 2;
  }
  DiskManager* disk = sm.GetDisk(args.seg);

  // 替换器（支持 clock 与可选 lruk）
  std::unique_ptr<IReplacer> replacer;
  if (args.replacer == "clock") {
    replacer = std::make_unique<ClockReplacer>(args.frames);
  }
#ifdef DBMS_STORAGE_ENABLE_LRUK
  else if (args.replacer == "lruk") {
    int k = args.k > 1 ? args.k : 2;
    replacer = std::make_unique<LruKReplacer>(args.frames, k);
  }
#endif
  else {
    std::cerr << "[WARN] unknown replacer: " << args.replacer
              << " -> fallback to clock\n";
    replacer = std::make_unique<ClockReplacer>(args.frames);
  }

  BufferPoolManager bpm(args.frames, args.page_size, disk, std::move(replacer));

  // FSM（按需设置分桶阈值）
  std::vector<uint32_t> bins = {128, 512, 1024, 2048, 4096, 8192, 16384};
  FreeSpaceManager fsm(args.page_size, bins);
  fsm.RegisterSegmentProbe(
    [&](seg_id_t seg, page_id_t pid){ return sm.ProbePageFree(seg, pid); },
    [&](seg_id_t seg){ return sm.PageCount(seg); }
  );

  TableHeap table(args.seg, args.page_size, &bpm, &fsm, &sm);
  Schema schema = MakeSupplierSchema();

  // === 读取并写入 ===
  std::ifstream fin(args.data_file);
  if (!fin) {
    std::cerr << "Open data file failed: " << args.data_file << "\n"; return 3;
  }

  std::cout << "[LOAD] begin: file=" << args.data_file
            << ", page_size=" << args.page_size
            << ", frames=" << args.frames
            << ", replacer=" << args.replacer
#ifdef DBMS_STORAGE_ENABLE_LRUK
            << (args.replacer == "lruk" ? ("(k=" + std::to_string(args.k) + ")") : "")
#endif
            << "\n";

  std::string line;
  size_t count = 0, bad = 0;
  while (std::getline(fin, line)) {
    if (line.empty()) continue;
    auto fields = SplitPipe(line);
    if (fields.size() != 7) { ++bad; continue; }

    TupleBuilder tb(schema);
    // suppkey | name | address | nationkey | phone | acctbal | comment
    tb.SetInt32 (0, std::stoi(fields[0]));
    tb.SetChar  (1, fields[1]);
    tb.SetVarChar(2, fields[2]);
    tb.SetInt32 (3, std::stoi(fields[3]));
    tb.SetChar  (4, fields[4]);
    tb.SetDouble(5, std::stod(fields[5]));
    tb.SetVarChar(6, fields[6]);

    Tuple t;
    Status bs = tb.Build(&t);
    if (!bs.ok()) { ++bad; continue; }

    RID rid;
    Status is = table.Insert(t, &rid);
    if (!is.ok()) { ++bad; continue; }
    ++count;

    if (args.log_every > 0 && (count % args.log_every == 0)) {
      auto st = bpm.GetStats();
      std::cout << "[PROGRESS] inserted=" << count
                << " hits=" << st.hits
                << " misses=" << st.misses
                << " evictions=" << st.evictions
                << " flushes=" << st.flushes
                << " pages=" << sm.PageCount(args.seg) << "\n";
      LogFsm(fsm);
    }
  }

  bpm.FlushAll(); (void)disk->Sync();

  auto st = bpm.GetStats();
  std::cout << "[LOAD] done: rows=" << count
            << " bad=" << bad
            << " pages=" << sm.PageCount(args.seg)
            << " | stats: hits=" << st.hits
            << ", misses=" << st.misses
            << ", evictions=" << st.evictions
            << ", flushes=" << st.flushes
            << "\n";

  // === 简单校验：全表扫描 5 行预览 ===
  size_t scan_cnt = 0, preview = 5;
  for (auto it = table.Begin(); it != table.End(); ++it) {
    const auto& row = *it;
    scan_cnt++;
    if (preview > 0) {
      int32_t suppkey = 0, nation = 0; double acctbal = 0.0;
      std::string name, phone;
      row.tuple.GetInt32 (schema, 0, &suppkey);
      row.tuple.GetChar  (schema, 1, &name);
      row.tuple.GetInt32 (schema, 3, &nation);
      row.tuple.GetChar  (schema, 4, &phone);
      row.tuple.GetDouble(schema, 5, &acctbal);
      std::cout << "[ROW] RID=(" << row.rid.page_id << "," << row.rid.slot
                << ") suppkey=" << suppkey
                << " name=\"" << name << "\""
                << " nation=" << nation
                << " phone=\"" << phone << "\""
                << " acctbal=" << acctbal << "\n";
      --preview;
    }
  }
  std::cout << "[SCAN] total rows = " << scan_cnt << "\n";
  LogFsm(fsm);
  return 0;
}
