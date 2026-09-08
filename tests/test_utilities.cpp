// ============================================================
// LPTA Standalone Unit Tests
//
// Tests for utility functions that don't require the full LLVM
// pass pipeline.
// ============================================================

#include <cassert>
#include <climits>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>

#include "Metrics.h"
#include "Tracker.h"
#include "JsonWriter.h"
#include "Snapshots.h"
#include "Detection.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/Support/SourceMgr.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(expr, msg) do { \
    if (expr) { g_pass++; printf("  [PASS] %s\n", msg); } \
    else      { g_fail++; printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while(0)

#define CHECK_EQ(a, b, msg) do { \
    auto _av = (a); auto _bv = (b); \
    if (_av == _bv) { g_pass++; printf("  [PASS] %s\n", msg); } \
    else { g_fail++; printf("  [FAIL] %s  (got %s vs %s)  (%s:%d)\n", \
           msg, #a, #b, __FILE__, __LINE__); } \
} while(0)

// ============================================================
// IRMetrics
// ============================================================
void test_metrics_zero_init() {
    IRMetrics m;
    CHECK(m.instruction_count == 0 && m.basic_block_count == 0 &&
          m.function_count == 0 && m.global_count == 0 &&
          m.call_count == 0 && m.load_count == 0 &&
          m.store_count == 0 && m.branch_count == 0 &&
          m.phi_count == 0 && m.return_count == 0,
          "IRMetrics: all fields zero-initialized");
}

void test_hasAnyDelta_same() {
    IRMetrics a, b;
    CHECK(!hasAnyDelta(a, b), "hasAnyDelta: identical -> false");
}

void test_hasAnyDelta_instr() {
    IRMetrics a, b;
    a.instruction_count = 10;
    b.instruction_count = 20;
    CHECK(hasAnyDelta(a, b), "hasAnyDelta: instruction_count differs -> true");
}

void test_hasAnyDelta_return() {
    IRMetrics a, b;
    a.return_count = 1;
    b.return_count = 2;
    CHECK(hasAnyDelta(a, b), "hasAnyDelta: return_count differs -> true");
}

void test_hasAnyDelta_extreme() {
    IRMetrics a, b;
    a.instruction_count = 0;
    b.instruction_count = UINT32_MAX;
    CHECK(hasAnyDelta(a, b), "hasAnyDelta: 0 vs UINT32_MAX -> true");
}

void test_hasAnyDelta_same_extreme() {
    IRMetrics a, b;
    a.instruction_count = UINT32_MAX;
    b.instruction_count = UINT32_MAX;
    CHECK(!hasAnyDelta(a, b), "hasAnyDelta: both UINT32_MAX -> false");
}

void test_hasAnyDelta_opgroup() {
    IRMetrics a, b;
    a.op_arith = 3;
    b.op_arith = 4;
    CHECK(hasAnyDelta(a, b), "hasAnyDelta: op_arith differs -> true");
}

// ============================================================
// jsonEscape
// ============================================================
void test_jsonEscape_empty() {
    CHECK(jsonEscape("").empty(), "jsonEscape: empty string");
}

void test_jsonEscape_plain() {
    CHECK(jsonEscape("hello") == "hello", "jsonEscape: plain text unchanged");
}

void test_jsonEscape_quotes() {
    CHECK(jsonEscape("say \"hi\"") == "say \\\"hi\\\"", "jsonEscape: double quotes");
}

void test_jsonEscape_backslash() {
    CHECK(jsonEscape("a\\b") == "a\\\\b", "jsonEscape: backslash");
}

void test_jsonEscape_newline() {
    CHECK(jsonEscape("a\nb") == "a\\nb", "jsonEscape: newline");
}

void test_jsonEscape_tab() {
    CHECK(jsonEscape("a\tb") == "a\\tb", "jsonEscape: tab");
}

void test_jsonEscape_slash() {
    CHECK(jsonEscape("a/b") == "a\\/b", "jsonEscape: forward slash");
}

void test_jsonEscape_control() {
    CHECK(jsonEscape(std::string(1, '\x01')) == "\\u0001",
          "jsonEscape: control char -> \\u0001");
}

void test_jsonEscape_backspace() {
    CHECK(jsonEscape("a\bc") == "a\\bc", "jsonEscape: backspace");
}

void test_jsonEscape_formfeed() {
    CHECK(jsonEscape("a\fc") == "a\\fc", "jsonEscape: formfeed");
}

void test_jsonEscape_cr() {
    CHECK(jsonEscape("a\rc") == "a\\rc", "jsonEscape: carriage return");
}

void test_jsonEscape_null() {
    CHECK(jsonEscape(std::string("a\0b", 3)).find("\\u0000") != std::string::npos,
          "jsonEscape: null byte -> \\u0000");
}

void test_jsonEscape_unicode_passthrough() {
    std::string cafe = "caf\xc3\xa9";
    CHECK(jsonEscape(cafe) == cafe, "jsonEscape: UTF-8 passes through");
}

void test_jsonEscape_long() {
    std::string s(500, 'x');
    s[250] = '"';
    std::string r = jsonEscape(s);
    CHECK(r.size() > s.size() && r.find("\\\"") != std::string::npos,
          "jsonEscape: long string with embedded quote");
}

// ============================================================
// classifyPass
// ============================================================
void test_classify_adaptor() {
    CHECK(classifyPass("AdaptorPass") == "adaptor", "classifyPass: Adaptor -> adaptor");
}

void test_classify_pipeline() {
    CHECK(classifyPass("PassManager") == "pipeline", "classifyPass: PassManager -> pipeline");
}

void test_classify_extra_pm() {
    CHECK(classifyPass("ExtraLoopPassManager") == "pipeline",
          "classifyPass: ExtraLoopPassManager -> pipeline");
}

void test_classify_analysis() {
    CHECK(classifyPass("LoopInfoAnalysis") == "analysis",
          "classifyPass: LoopInfoAnalysis -> analysis");
}

void test_classify_require() {
    CHECK(classifyPass("RequireAnalysis<LoopInfo>") == "analysis",
          "classifyPass: RequireAnalysis -> analysis");
}

void test_classify_invalidate() {
    CHECK(classifyPass("InvalidateAnalysis<LoopPass>") == "adaptor",
          "classifyPass: InvalidateAnalysis -> adaptor (priority over analysis)");
}

void test_classify_transform() {
    CHECK(classifyPass("InstCombinePass") == "transformation",
          "classifyPass: InstCombinePass -> transformation");
}

void test_classify_simplify() {
    CHECK(classifyPass("SimplifyCFGPass") == "transformation",
          "classifyPass: SimplifyCFGPass -> transformation");
}

void test_classify_empty() {
    CHECK(classifyPass("") == "transformation",
          "classifyPass: empty -> transformation");
}

// ============================================================
// irUnitKindName
// ============================================================
void test_kind_names() {
    CHECK(std::string(irUnitKindName(IRUnitKind::Module)) == "Module",
          "irUnitKindName: Module");
    CHECK(std::string(irUnitKindName(IRUnitKind::Function)) == "Function",
          "irUnitKindName: Function");
    CHECK(std::string(irUnitKindName(IRUnitKind::Loop)) == "Loop",
          "irUnitKindName: Loop");
    CHECK(std::string(irUnitKindName(IRUnitKind::Unknown)) == "Unknown",
          "irUnitKindName: Unknown");
}

// ============================================================
// sanitizeFilename
// ============================================================
void test_sanitize_normal() {
    CHECK(sanitizeFilename("hello_world") == "hello_world",
          "sanitizeFilename: normal name unchanged");
}

void test_sanitize_special() {
    CHECK(sanitizeFilename("a/b:c d") == "a_b_c_d",
          "sanitizeFilename: special chars replaced");
}

void test_sanitize_empty() {
    CHECK(sanitizeFilename("") == "unnamed",
          "sanitizeFilename: empty -> unnamed");
}

void test_sanitize_dots() {
    CHECK(sanitizeFilename("file.ll") == "file.ll",
          "sanitizeFilename: dots preserved");
}

void test_sanitize_angle() {
    std::string r = sanitizeFilename("T<int>");
    CHECK(r.find('<') == std::string::npos && r.find('>') == std::string::npos,
          "sanitizeFilename: angle brackets replaced");
}

void test_sanitize_length_cap() {
    std::string long_name(500, 'a');
    std::string r = sanitizeFilename(long_name);
    CHECK(r.size() <= 120, "sanitizeFilename: long names truncated to cap");
    CHECK(r == sanitizeFilename(long_name), "sanitizeFilename: truncation deterministic");
    CHECK(sanitizeFilename("short") == "short", "sanitizeFilename: short names untouched");
}

void test_sanitize_windows_reserved() {
    CHECK(sanitizeFilename("CON") == "CON_", "sanitizeFilename: CON suffixed");
    CHECK(sanitizeFilename("nul") == "nul_", "sanitizeFilename: nul (any case) suffixed");
    CHECK(sanitizeFilename("COM1") == "COM1_", "sanitizeFilename: COM1 suffixed");
    CHECK(sanitizeFilename("nul.ll") == "nul.ll", "sanitizeFilename: extension makes it legal");
    CHECK(sanitizeFilename("trailing.") == "trailing._", "sanitizeFilename: trailing dot suffixed");
    CHECK(sanitizeFilename("plain") == "plain", "sanitizeFilename: normal names untouched");
}

// ============================================================
// shouldSnapshot
// ============================================================
void test_snapshot_default_off() {
    CHECK(!shouldSnapshot("InstCombinePass"),
          "shouldSnapshot: defaults to off even for allowed pass");
}

// ============================================================
// g_snapshot_allowlist
// ============================================================
void test_allowlist_contents() {
    CHECK(g_snapshot_allowlist.count("InstCombinePass") &&
          g_snapshot_allowlist.count("SimplifyCFGPass") &&
          g_snapshot_allowlist.count("GVNPass") &&
          g_snapshot_allowlist.count("LICMPass") &&
          g_snapshot_allowlist.count("LoopUnrollPass") &&
          g_snapshot_allowlist.count("InlinerPass"),
          "g_snapshot_allowlist: contains expected passes");
}

void test_allowlist_exclusion() {
    CHECK(!g_snapshot_allowlist.count("NonExistentPass"),
          "g_snapshot_allowlist: does not contain random pass");
}

// ============================================================
// CodegenResult defaults
// ============================================================
void test_codegen_defaults() {
    CodegenResult cr;
    CHECK(cr.asm_lines_before == 0 && cr.asm_lines_after == 0 &&
          cr.asm_size_before == 0 && cr.asm_size_after == 0,
          "CodegenResult: all fields zero-initialized");
}

// ============================================================
// printDeltaLine (crash test)
// ============================================================
void test_delta_no_change() {
    printDeltaLine("test", 10, 10);
    CHECK(true, "printDeltaLine: no change -> no crash");
}

void test_delta_large() {
    unsigned big = 3000000000u;
    printDeltaLine("test", big, big - 100);
    CHECK(true, "printDeltaLine: large values (previously overflowed) -> no crash");
}

void test_delta_max() {
    printDeltaLine("test", UINT32_MAX, UINT32_MAX - 1);
    CHECK(true, "printDeltaLine: near-max unsigned -> no crash");
}

// ============================================================
// Struct defaults
// ============================================================
void test_event_init() {
    Event e;
    CHECK(e.id == 0 && e.event_type.empty() && e.pass_name.empty() &&
          e.depth == 0 && !e.has_changes && e.ir_before.empty(),
          "Event: default initialization correct");
}

void test_passframe_init() {
    PassFrame f;
    CHECK(f.ir_ptr == nullptr && f.ir_kind == IRUnitKind::Unknown &&
          f.depth == 0 && !f.invalidated && f.event_id == 0,
          "PassFrame: default initialization correct");
}

// ============================================================
// writeMetricsJSON
// ============================================================
void test_metrics_json_format() {
    IRMetrics m;
    m.instruction_count = 42;
    m.return_count = 2;
    std::ostringstream oss;
    writeMetricsJSON(oss, m, "  ");
    std::string j = oss.str();
    CHECK(j.find("\"instruction_count\": 42") != std::string::npos &&
          j.find("\"return_count\": 2") != std::string::npos,
          "writeMetricsJSON: correct JSON field output");
}

// ============================================================
// captureFunctionMetrics / captureModuleMetrics (real IR)
// ============================================================

static std::unique_ptr<llvm::Module>
parseTestIR(llvm::LLVMContext &Ctx, const char *IR) {
    llvm::SMDiagnostic Err;
    return llvm::parseAssemblyString(IR, Err, Ctx);
}

static const char kInvokeIR[] = R"ir(
    declare void @f()
    declare i32 @__gxx_personality_v0(...)

    define i32 @with_invoke() personality ptr @__gxx_personality_v0 {
    entry:
      invoke void @f() to label %cont unwind label %lpad
    cont:
      ret i32 0
    lpad:
      %e = landingpad { ptr, i32 } cleanup
      resume { ptr, i32 } %e
    }
)ir";

static const char kCallbrIR[] = R"ir(
    define void @with_callbr() {
    entry:
      callbr void asm "", "r,!i"(i32 0)
              to label %normal [label %other]
    normal:
      ret void
    other:
      ret void
    }
)ir";

void test_invoke_function_metrics() {
    llvm::LLVMContext Ctx;
    auto M = parseTestIR(Ctx, kInvokeIR);
    CHECK(M != nullptr, "parseTestIR: invoke IR parses");
    if (!M) return;
    IRMetrics m = captureFunctionMetrics(*M->getFunction("with_invoke"));
    CHECK_EQ(m.call_count, 1u,
             "captureFunctionMetrics: invoke counted as call");
    CHECK_EQ(m.instruction_count, 4u,
             "captureFunctionMetrics: invoke instruction_count");
}

void test_callbr_function_metrics() {
    llvm::LLVMContext Ctx;
    auto M = parseTestIR(Ctx, kCallbrIR);
    CHECK(M != nullptr, "parseTestIR: callbr IR parses");
    if (!M) return;
    IRMetrics m = captureFunctionMetrics(*M->getFunction("with_callbr"));
    CHECK_EQ(m.call_count, 1u,
             "captureFunctionMetrics: callbr counted as call");
}

void test_module_function_call_agreement() {
    llvm::LLVMContext Ctx;
    auto M = parseTestIR(Ctx, kInvokeIR);
    CHECK(M != nullptr, "parseTestIR: agreement IR parses");
    if (!M) return;
    unsigned fn_calls = 0, fn_instrs = 0;
    for (const llvm::Function &F : *M) {
        if (F.isDeclaration()) continue;
        IRMetrics m = captureFunctionMetrics(F);
        fn_calls += m.call_count;
        fn_instrs += m.instruction_count;
    }
    IRMetrics mod = captureModuleMetrics(*M);
    CHECK_EQ(mod.call_count, fn_calls,
             "module vs function metrics: call counts agree");
    CHECK_EQ(mod.instruction_count, fn_instrs,
             "module vs function metrics: instruction counts agree");
}

// ============================================================
// hashIRUnit / hashIRText (real IR-change detection)
// ============================================================
void test_hash_deterministic() {
    llvm::LLVMContext Ctx;
    auto M = parseTestIR(Ctx, kInvokeIR);
    CHECK(M != nullptr, "parseTestIR: hash determinism IR parses");
    if (!M) return;
    llvm::Any a = static_cast<const llvm::Function *>(M->getFunction("with_invoke"));
    CHECK(hashIRUnit(a) == hashIRUnit(a), "hashIRUnit: same IR -> same hash");
    CHECK(hashIRUnit(a) != 0, "hashIRUnit: nonzero for real IR");
}

void test_hash_sensitive_to_operands() {
    static const char kA[] = "define i32 @f(i32 %x) {\nentry:\n  %a = add i32 %x, 1\n  ret i32 %a\n}\n";
    static const char kB[] = "define i32 @f(i32 %x) {\nentry:\n  %a = add i32 %x, 2\n  ret i32 %a\n}\n";
    llvm::LLVMContext CtxA, CtxB;
    auto MA = parseTestIR(CtxA, kA);
    auto MB = parseTestIR(CtxB, kB);
    CHECK(MA != nullptr && MB != nullptr, "parseTestIR: operand-variant IR parses");
    if (!MA || !MB) return;
    llvm::Any a = static_cast<const llvm::Function *>(MA->getFunction("f"));
    llvm::Any b = static_cast<const llvm::Function *>(MB->getFunction("f"));
    IRMetrics mA = captureFunctionMetrics(*MA->getFunction("f"));
    IRMetrics mB = captureFunctionMetrics(*MB->getFunction("f"));
    CHECK(!hasAnyDelta(mA, mB), "hash test setup: operand edit moves no counters");
    CHECK(hashIRUnit(a) != hashIRUnit(b), "hashIRUnit: add 1 vs add 2 -> different hash");
}

void test_hash_unknown_is_zero() {
    llvm::Any empty;
    CHECK(hashIRUnit(empty) == 0, "hashIRUnit: Unknown unit -> 0 (never reports change)");
    CHECK(hashIRText("") == 0, "hashIRText: empty text -> 0");
    CHECK(hashIRText("x") != 0, "hashIRText: nonempty text -> nonzero");
}

void test_opcode_partition() {
    llvm::LLVMContext Ctx;
    auto M = parseTestIR(Ctx, kInvokeIR);
    CHECK(M != nullptr, "parseTestIR: partition IR parses");
    if (!M) return;
    IRMetrics m = captureFunctionMetrics(*M->getFunction("with_invoke"));
    // with_invoke: invoke (->call), ret+resume (->control), landingpad (->other)
    CHECK_EQ(m.op_call, 1u, "partition: invoke counted in op_call");
    CHECK_EQ(m.op_control, 2u, "partition: ret+resume counted in op_control");
    CHECK_EQ(m.op_other, 1u, "partition: landingpad counted in op_other");
    unsigned sum = m.op_arith + m.op_cmp + m.op_memory + m.op_control +
                   m.op_cast + m.op_call + m.op_vector + m.op_other;
    CHECK_EQ(sum, m.instruction_count, "partition: groups sum to instruction_count");
}

// ============================================================
// writeHistoryJSON schema stability
// ============================================================

void test_history_json_empty_events_schema() {
    g_events.clear();
    const char *fn = "test_history_empty_tmp.json";
    writeHistoryJSON(fn);
    std::ifstream f(fn);
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    std::remove(fn);
    CHECK(content.find("\"total_instructions_before\": 0") != std::string::npos &&
          content.find("\"total_instructions_after\": 0") != std::string::npos &&
          content.find("\"total_bbs_before\": 0") != std::string::npos &&
          content.find("\"total_bbs_after\": 0") != std::string::npos,
          "writeHistoryJSON: zero-event run still emits total_* summary keys");
}

// ============================================================
// Main
// ============================================================
int main() {
    printf("=== LPTA Unit Tests ===\n\n");

    printf("IRMetrics:\n");
    test_metrics_zero_init();
    test_hasAnyDelta_same();
    test_hasAnyDelta_instr();
    test_hasAnyDelta_return();
    test_hasAnyDelta_extreme();
    test_hasAnyDelta_same_extreme();
    test_hasAnyDelta_opgroup();

    printf("\njsonEscape:\n");
    test_jsonEscape_empty();
    test_jsonEscape_plain();
    test_jsonEscape_quotes();
    test_jsonEscape_backslash();
    test_jsonEscape_newline();
    test_jsonEscape_tab();
    test_jsonEscape_slash();
    test_jsonEscape_control();
    test_jsonEscape_backspace();
    test_jsonEscape_formfeed();
    test_jsonEscape_cr();
    test_jsonEscape_null();
    test_jsonEscape_unicode_passthrough();
    test_jsonEscape_long();

    printf("\nclassifyPass:\n");
    test_classify_adaptor();
    test_classify_pipeline();
    test_classify_extra_pm();
    test_classify_analysis();
    test_classify_require();
    test_classify_invalidate();
    test_classify_transform();
    test_classify_simplify();
    test_classify_empty();

    printf("\nirUnitKindName:\n");
    test_kind_names();

    printf("\nhashIRUnit / hashIRText:\n");
    test_hash_deterministic();
    test_hash_sensitive_to_operands();
    test_hash_unknown_is_zero();
    test_opcode_partition();

    printf("\nsanitizeFilename:\n");
    test_sanitize_normal();
    test_sanitize_special();
    test_sanitize_empty();
    test_sanitize_dots();
    test_sanitize_angle();
    test_sanitize_length_cap();
    test_sanitize_windows_reserved();

    printf("\nshouldSnapshot / allowlist:\n");
    test_snapshot_default_off();
    test_allowlist_contents();
    test_allowlist_exclusion();

    printf("\nCodegenResult:\n");
    test_codegen_defaults();

    printf("\nprintDeltaLine (crash / overflow):\n");
    test_delta_no_change();
    test_delta_large();
    test_delta_max();

    printf("\nStruct defaults:\n");
    test_event_init();
    test_passframe_init();

    printf("\nwriteMetricsJSON:\n");
    test_metrics_json_format();

    printf("\ncaptureFunctionMetrics (real IR):\n");
    test_invoke_function_metrics();
    test_callbr_function_metrics();
    test_module_function_call_agreement();

    printf("\nwriteHistoryJSON schema:\n");
    test_history_json_empty_events_schema();

    printf("\n=== Results: %d passed, %d failed (out of %d) ===\n",
           g_pass, g_fail, g_pass + g_fail);
    return g_fail > 0 ? 1 : 0;
}
