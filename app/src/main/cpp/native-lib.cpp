// main.cpp — C++ port of the Python Stratum demo (feature parity, same screens/behaviour)
// + NEW "Arrays" page that exercises typed / multi-dimensional Java arrays, reflect vectors,
//   named ObjArr arguments, ArrayStoreException safety, and two new benchmark rows.
//
// Rules this file follows (learned from the SDK design):
//   * A Java class's default C++ constructor is a NULL object. A real Java no-arg
//     constructor is  Cls::create()  (e.g. Paint::create()).  Never write Paint().
//   * Every callback body goes through guard(): an exception escaping a callback is
//     turned into a Java RuntimeException on the UI thread (= app crash).
//   * No int/uint32 colour ambiguity: every colour goes through C() -> int32_t
//     (Paint.setColor / Canvas.drawColor have both int and long overloads).
#include <stratum/stratum.hpp>
#include <stratum/reflect.hpp>   // NEW: reflect is not pulled in by stratum.hpp

#include <stratum/android/widget/LinearLayout.hpp>
#include <stratum/android/widget/FrameLayout.hpp>
#include <stratum/android/widget/ScrollView.hpp>
#include <stratum/android/widget/HorizontalScrollView.hpp>
#include <stratum/android/widget/TextView.hpp>
#include <stratum/android/widget/Button.hpp>
#include <stratum/android/widget/EditText.hpp>
#include <stratum/android/widget/Toast.hpp>
#include <stratum/android/widget/SeekBar.hpp>
#include <stratum/android/widget/CheckBox.hpp>
#include <stratum/android/widget/LinearLayout_LayoutParams.hpp>
#include <stratum/android/view/View.hpp>
#include <stratum/android/view/MotionEvent.hpp>
#include <stratum/android/graphics/Paint.hpp>
#include <stratum/android/graphics/Paint_Style.hpp>
#include <stratum/android/graphics/Canvas.hpp>
#include <stratum/android/content/res/Resources.hpp>
#include <stratum/android/util/DisplayMetrics.hpp>
#include <stratum/android/content/ClipboardManager.hpp>
#include <stratum/android/content/ClipData.hpp>
#include <stratum/android/hardware/SensorManager.hpp>
#include <stratum/android/hardware/Sensor.hpp>
#include <stratum/android/hardware/SensorEvent.hpp>
#include <stratum/android/os/Build.hpp>
#include <stratum/android/os/Build_VERSION.hpp>
#include <stratum/java/lang/Integer.hpp>
#include <stratum/java/lang/String.hpp>
#include <stratum/java/util/Arrays.hpp>

#include <android/log.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// Set to 1 AFTER you confirmed (grep API_INDEX.txt for "java.lang.String: join(") that the generated
// header has a static  join(JStr, const ObjArr&)  overload. It is the real "typed 'A' parameter"
// test: String.join(CharSequence, CharSequence...) needs a CharSequence[] (old engine passed Object[]).
#ifndef STRATUM_DEMO_DIRECT_JOIN
#define STRATUM_DEMO_DIRECT_JOIN 0
#endif

using stratum::Object;
using stratum::JStr;
using stratum::Handlers;
using stratum::Activity;
using stratum::CustomCanvasView;
using stratum::android::view::View;
using stratum::android::view::MotionEvent;
using stratum::android::widget::LinearLayout;
using stratum::android::widget::LinearLayout_LayoutParams;
using stratum::android::widget::FrameLayout;
using stratum::android::widget::ScrollView;
using stratum::android::widget::HorizontalScrollView;
using stratum::android::widget::TextView;
using stratum::android::widget::Button;
using stratum::android::widget::EditText;
using stratum::android::widget::Toast;
using stratum::android::widget::SeekBar;
using stratum::android::widget::CheckBox;
using stratum::android::graphics::Paint;
using stratum::android::graphics::Paint_Style;
using stratum::android::graphics::Canvas;
using stratum::android::content::ClipboardManager;
using stratum::android::content::ClipData;
using stratum::android::hardware::SensorManager;
using stratum::android::hardware::Sensor;
using stratum::android::hardware::SensorEvent;
using stratum::android::os::Build;
using stratum::android::os::Build_VERSION;
using JString = stratum::java::lang::String;
using JInteger = stratum::java::lang::Integer;
using stratum::java::util::Arrays;

// ─────────────────────────────────────────────────────────────────────────────
// Small utilities
// ─────────────────────────────────────────────────────────────────────────────
static inline int32_t C(uint32_t c) { return static_cast<int32_t>(c); }

static void logE(const char* m) { __android_log_print(ANDROID_LOG_ERROR, "StratumDemo", "%s", m); }

static std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
static std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

static size_t utf8_len(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

static size_t word_count(const std::string& s) {
    std::istringstream is(s);
    std::string w;
    size_t n = 0;
    while (is >> w) ++n;
    return n;
}

static std::string commas(long long v) {
    std::string s = std::to_string(v < 0 ? -v : v), out;
    int c = 0;
    for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i) {
        out.insert(out.begin(), s[i]);
        if (++c % 3 == 0 && i > 0) out.insert(out.begin(), ',');
    }
    return v < 0 ? "-" + out : out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Theme + global state
// ─────────────────────────────────────────────────────────────────────────────
struct Theme { uint32_t bg, card, text, sub; };
static const Theme DARK_T  {0xFF101418, 0xFF1B222B, 0xFFEAF0F6, 0xFF8FA0B3};
static const Theme LIGHT_T {0xFFF3F5F8, 0xFFFFFFFF, 0xFF1A2330, 0xFF5B6B7D};
static constexpr uint32_t ACCENT = 0xFF00BFA5, ACCENT2 = 0xFF3D7BFF, DANGER = 0xFFE5484D, NAVOFF = 0xFF37474F;

static Theme g_theme = DARK_T;
static Activity g_act;
static float g_density = 2.75f;

static std::vector<std::pair<View, std::string>> g_themed_views;
static std::vector<std::pair<TextView, std::string>> g_themed_texts;
static std::unordered_map<std::string, View> g_pages;
static std::unordered_map<std::string, Button> g_nav_btns;
static std::string g_current_page = "home";
static TextView g_title_tv;
static FrameLayout g_content_frame;

static const std::vector<std::pair<std::string, std::string>> NAV = {
        {"home", "Home"}, {"todo", "Todo"}, {"calc", "Calc"}, {"paint", "Paint"}, {"sensors", "Level"},
        {"timer", "Timer"}, {"notes", "Notes"}, {"bench", "Bench"}, {"arrays", "Arrays"}, {"settings", "Settings"}};

static std::string page_title(const std::string& n) {
    static const std::unordered_map<std::string, std::string> T = {
            {"home", "Stratum Demo"}, {"todo", "Tasks"}, {"calc", "Calculator"}, {"paint", "Sketchpad"},
            {"sensors", "Level & Motion"}, {"timer", "Stopwatch"}, {"notes", "Notes"},
            {"bench", "Bridge Benchmark"}, {"arrays", "Java Arrays"}, {"settings", "Settings"}};
    auto it = T.find(n);
    return it == T.end() ? n : it->second;
}

static int dp(float x) { return static_cast<int>(x * g_density); }

// ─────────────────────────────────────────────────────────────────────────────
// Toast / guard
// ─────────────────────────────────────────────────────────────────────────────
static void toast(const std::string& msg) {
    try {
        Toast::makeText(g_act, JStr(msg), 0).unchecked_as<Toast>().show();
    } catch (const std::exception& e) {
        logE(e.what());
    } catch (...) {}
}

template <class F>
static void guard(F&& f) noexcept {
try {
f();
} catch (const std::exception& e) {
logE(e.what());
toast(std::string("Error: ") + e.what());
} catch (...) {
logE("unknown error in callback");
}
}

template <class T>
static void set_text(const T& v, const std::string& s) { v.setText(JStr(s)); }

// ─────────────────────────────────────────────────────────────────────────────
// UI toolkit (mirrors the Python helpers)
// ─────────────────────────────────────────────────────────────────────────────
static void paint_view_role(const View& v, const std::string& role) {
    if (role == "bg") v.setBackgroundColor(C(g_theme.bg));
    else if (role == "card") v.setBackgroundColor(C(g_theme.card));
}

static void paint_text_role(const TextView& tv, const std::string& role) {
    if (role == "text") tv.setTextColor(C(g_theme.text));
    else if (role == "sub") tv.setTextColor(C(g_theme.sub));
    else if (role == "input") {
        tv.setBackgroundColor(C(g_theme.card));
        tv.setTextColor(C(g_theme.text));
        tv.setHintTextColor(C(g_theme.sub));
    }
}

static void themed_view(const View& v, const std::string& role) {
    g_themed_views.emplace_back(v, role);
    paint_view_role(v, role);
}

static void themed_text(const TextView& tv, const std::string& role) {
    g_themed_texts.emplace_back(tv, role);
    paint_text_role(tv, role);
}

template <class P, class Ch>
static void add(const P& parent, const Ch& child, int w = -1, int h = -2, float weight = -1.0f,
                int ml = 0, int mt = 0, int mr = 0, int mb = 0) {
    LinearLayout_LayoutParams p = (weight >= 0.0f) ? LinearLayout_LayoutParams(w, h, weight)
                                                   : LinearLayout_LayoutParams(w, h);
    if (ml || mt || mr || mb) p.setMargins(dp(ml), dp(mt), dp(mr), dp(mb));
    parent.addView(child, p);
}

static LinearLayout vbox(const char* role = nullptr, int pad = 0) {
    LinearLayout b(g_act);
    b.setOrientation(1);
    if (pad) b.setPadding(dp(pad), dp(pad), dp(pad), dp(pad));
    if (role) themed_view(b.unchecked_as<View>(), role);
    return b;
}

static LinearLayout hbox(const char* role = nullptr, int pad = 0) {
    LinearLayout b = vbox(role, pad);
    b.setOrientation(0);
    return b;
}

static TextView label(const std::string& text, float size = 14.0f, const char* role = "text", int gravity = -1) {
    TextView tv(g_act);
    set_text(tv, text);
    tv.setTextSize(size);
    if (gravity != -1) tv.setGravity(gravity);
    themed_text(tv, role);
    return tv;
}

template <class Fn>
static Button button(const std::string& text, Fn&& fn, uint32_t color = ACCENT, float size = 14.0f) {
    Button b(g_act);
    set_text(b, text);
    b.setTextSize(size);
    b.setTextColor(C(0xFFFFFFFF));
    b.setBackgroundColor(C(color));
    try { b.setAllCaps(false); } catch (...) {}
    b.setOnClickListener(std::forward<Fn>(fn));
    return b;
}

static LinearLayout card(const LinearLayout& parent, int pad = 12) {
    LinearLayout c = vbox("card", pad);
    add(parent, c, -1, -2, -1.0f, 0, 0, 0, 10);
    return c;
}

static EditText edit(const std::string& hint = "", int lines = 1) {
    EditText et(g_act);
    et.setHint(JStr(hint));
    if (lines == 1) {
        et.setSingleLine(true);
    } else {
        et.setLines(lines);
        et.setGravity(48);  // Gravity.TOP
    }
    et.setPadding(dp(10), dp(10), dp(10), dp(10));
    themed_text(et.unchecked_as<TextView>(), "input");
    return et;
}

static LinearLayout new_page(const std::string& name, bool scroll = true) {
    LinearLayout content;
    View view;
    if (scroll) {
        ScrollView sv(g_act);
        sv.setFillViewport(true);
        content = vbox("bg", 14);
        sv.addView(content);
        themed_view(sv.unchecked_as<View>(), "bg");
        view = sv.unchecked_as<View>();
    } else {
        content = vbox("bg", 10);
        view = content.unchecked_as<View>();
    }
    view.setVisibility(8);  // GONE
    g_content_frame.addView(view);
    g_pages[name] = view;
    return content;
}

static void refresh_home();
static void start_sensors();

static void show_page(const std::string& name) {
    g_current_page = name;
    for (auto& kv : g_pages) kv.second.setVisibility(kv.first == name ? 0 : 8);
    for (auto& kv : g_nav_btns) kv.second.setBackgroundColor(C(kv.first == name ? ACCENT : NAVOFF));
    set_text(g_title_tv, page_title(name));
    if (name == "home") refresh_home();
    else if (name == "sensors") start_sensors();
}

// ─────────────────────────────────────────────────────────────────────────────
// Device info
// ─────────────────────────────────────────────────────────────────────────────
struct Specs { std::string os = "Android", api = "?", hw = "?", board = "?", abi, cores; };
static Specs g_specs;

static void load_specs() {
    Specs s;
#if defined(__aarch64__)
    s.abi = "arm64-v8a";
#elif defined(__x86_64__)
    s.abi = "x86_64";
#elif defined(__arm__)
    s.abi = "armeabi-v7a";
#else
    s.abi = "x86";
#endif
    s.cores = std::to_string(std::thread::hardware_concurrency());
    try { s.os = "Android " + std::string(Build_VERSION::sf_get_RELEASE()); } catch (...) {}
    try { s.api = std::to_string(Build_VERSION::sf_get_SDK_INT()); } catch (...) {}
    try { s.hw = Build::sf_get_HARDWARE(); } catch (...) {}
    try { s.board = Build::sf_get_BOARD(); } catch (...) {}
    g_specs = s;
}

static std::string specs_text() {
    return fmt("OS: %s (API %s)\nSoC: %s / %s\nCPU: %s cores (%s)\nRuntime: C++17 native (Stratum C ABI v%d)",
               g_specs.os.c_str(), g_specs.api.c_str(), g_specs.hw.c_str(), g_specs.board.c_str(),
               g_specs.cores.c_str(), g_specs.abi.c_str(), stratum_abi_version());
}

// ─────────────────────────────────────────────────────────────────────────────
// Persistence (app private files dir)
// ─────────────────────────────────────────────────────────────────────────────
static std::string data_dir() {
    static std::string d;
    if (d.empty()) {
        try { d = g_act.getFilesDir().to_string(); } catch (...) {}
    }
    return d;
}
static std::string todo_path() { return data_dir() + "/todos.txt"; }
static std::string notes_path() { return data_dir() + "/notes.txt"; }

// ─────────────────────────────────────────────────────────────────────────────
// 1. HOME
// ─────────────────────────────────────────────────────────────────────────────
struct Todo { std::string t; bool d; };
static std::vector<Todo> g_todos;
static std::string g_notes_text;

static TextView g_home_date, g_home_stats;

static void build_home() {
    LinearLayout c = new_page("home");
    add(c, label("Stratum Demo", 26.0f), -1, -2, -1.0f, 0, 4, 0, 0);
    add(c, label("A multi-screen Android app written 100% in C++.", 13.0f, "sub"), -1, -2, -1.0f, 0, 0, 0, 10);
    g_home_date = label("", 15.0f);
    add(c, g_home_date, -1, -2, -1.0f, 0, 0, 0, 6);
    g_home_stats = label("", 13.0f, "sub");
    add(c, g_home_stats, -1, -2, -1.0f, 0, 0, 0, 12);

    LinearLayout info = card(c);
    add(info, label("This device", 13.0f, "sub"));
    add(info, label(specs_text(), 14.0f));

    add(c, label("Quick launch", 13.0f, "sub"), -1, -2, -1.0f, 0, 4, 0, 6);
    const std::vector<std::pair<std::string, std::string>> grid = {
            {"todo", "Tasks"}, {"calc", "Calculator"}, {"paint", "Sketchpad"}, {"sensors", "Level"},
            {"timer", "Stopwatch"}, {"notes", "Notes"}, {"bench", "Benchmark"}, {"arrays", "Java Arrays"},
            {"settings", "Settings"}};
    const uint32_t colors[2] = {ACCENT, ACCENT2};
    for (size_t i = 0; i < grid.size(); i += 2) {
        LinearLayout row = hbox();
        for (size_t j = 0; j < 2 && i + j < grid.size(); ++j) {
            const std::string name = grid[i + j].first;
            add(row, button(grid[i + j].second, [name]() { guard([&] { show_page(name); }); },
                            colors[(i / 2 + j) % 2], 15.0f),
                0, dp(56), 1.0f, 3, 3, 3, 3);
        }
        add(c, row);
    }
}

static void refresh_home() {
    char buf[96];
    std::time_t t = std::time(nullptr);
    std::tm tmv;
    localtime_r(&t, &tmv);
    std::strftime(buf, sizeof(buf), "%A, %d %B %Y   %H:%M", &tmv);
    set_text(g_home_date, buf);
    int open_n = 0;
    for (auto& t2 : g_todos) if (!t2.d) ++open_n;
    set_text(g_home_stats, fmt("Open tasks: %d  |  Note chars: %d  |  Live callbacks: %d", open_n,
                               static_cast<int>(utf8_len(g_notes_text)), static_cast<int>(stratum_callback_count())));
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. TODO (persisted)
// ─────────────────────────────────────────────────────────────────────────────
static LinearLayout g_todo_list;
static TextView g_todo_count;
static EditText g_todo_input;

static void load_todos() {
    g_todos.clear();
    std::ifstream f(todo_path());
    if (!f) {
        g_todos = {{"Try tapping this task", false}, {"Add your own above", false}};
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() < 2) continue;
        g_todos.push_back({line.substr(2), line[0] == '1'});
    }
}

static void save_todos() {
    std::ofstream f(todo_path(), std::ios::trunc);
    if (!f) { logE("save_todos failed"); return; }
    for (auto& t : g_todos) f << (t.d ? '1' : '0') << '\t' << t.t << '\n';
}

static void render_todos();

static void toggle_todo(size_t i) {
    if (i >= g_todos.size()) return;
    g_todos[i].d = !g_todos[i].d;
    save_todos();
    render_todos();
}

static void render_todos() {
    if (!g_todo_list) return;
    g_todo_list.removeAllViews();
    int open_n = 0;
    for (size_t i = 0; i < g_todos.size(); ++i) {
        if (!g_todos[i].d) ++open_n;
        TextView tv(g_act);
        set_text(tv, (g_todos[i].d ? "[x]  " : "[  ]  ") + g_todos[i].t);
        tv.setTextSize(16.0f);
        tv.setPadding(dp(12), dp(12), dp(12), dp(12));
        tv.setBackgroundColor(C(g_theme.card));
        tv.setTextColor(C(g_todos[i].d ? g_theme.sub : g_theme.text));
        tv.setOnClickListener([i]() { guard([&] { toggle_todo(i); }); });
        add(g_todo_list, tv, -1, -2, -1.0f, 0, 3, 0, 3);
    }
    set_text(g_todo_count, fmt("%d open / %d total", open_n, static_cast<int>(g_todos.size())));
}

static void add_todo() {
    std::string text = trim(g_todo_input.getText().to_string());
    if (text.empty()) { toast("Type something first"); return; }
    g_todos.push_back({text, false});
    set_text(g_todo_input, "");
    save_todos();
    render_todos();
}

static void clear_done() {
    g_todos.erase(std::remove_if(g_todos.begin(), g_todos.end(), [](const Todo& t) { return t.d; }), g_todos.end());
    save_todos();
    render_todos();
}

static void build_todo() {
    LinearLayout c = new_page("todo");
    g_todo_input = edit("New task...");
    add(c, g_todo_input);
    LinearLayout row = hbox();
    add(row, button("Add", []() { guard(add_todo); }), 0, -2, 1.0f, 0, 4, 4, 4);
    add(row, button("Clear done", []() { guard(clear_done); }, DANGER), 0, -2, 1.0f, 4, 4, 0, 4);
    add(c, row);
    g_todo_count = label("", 12.0f, "sub");
    add(c, g_todo_count, -1, -2, -1.0f, 0, 6, 0, 6);
    g_todo_list = vbox();
    add(c, g_todo_list);
    render_todos();
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. CALCULATOR (real expression evaluator: precedence, parentheses, unary, //)
// ─────────────────────────────────────────────────────────────────────────────
struct CalcParser {
    const std::string& s;
    size_t i = 0;
    explicit CalcParser(const std::string& str) : s(str) {}
    void ws() { while (i < s.size() && s[i] == ' ') ++i; }
    bool peek(char c) { ws(); return i < s.size() && s[i] == c; }
    double expr() {
        double v = term();
        for (;;) {
            if (peek('+')) { ++i; v += term(); }
            else if (peek('-')) { ++i; v -= term(); }
            else return v;
        }
    }
    double term() {
        double v = unary();
        for (;;) {
            if (peek('*')) { ++i; v *= unary(); }
            else if (peek('/')) {
                ++i;
                bool floor_div = false;
                if (i < s.size() && s[i] == '/') { floor_div = true; ++i; }
                double r = unary();
                if (r == 0.0) throw std::runtime_error("division by zero");
                v = floor_div ? std::floor(v / r) : v / r;
            } else return v;
        }
    }
    double unary() {
        if (peek('+')) { ++i; return unary(); }
        if (peek('-')) { ++i; return -unary(); }
        return primary();
    }
    double primary() {
        ws();
        if (i < s.size() && s[i] == '(') {
            ++i;
            double v = expr();
            if (!peek(')')) throw std::runtime_error("missing )");
            ++i;
            return v;
        }
        size_t st = i;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
        if (st == i) throw std::runtime_error("number expected");
        std::string num = s.substr(st, i - st);
        char* end = nullptr;
        double v = std::strtod(num.c_str(), &end);
        if (!end || *end) throw std::runtime_error("bad number");
        return v;
    }
};

static std::string calc_eval(const std::string& expr) {
    if (expr.empty() || expr.find("**") != std::string::npos) throw std::runtime_error("bad expression");
    for (char ch : expr)
        if (!(std::isdigit(static_cast<unsigned char>(ch)) || std::string("+-*/.() ").find(ch) != std::string::npos))
            throw std::runtime_error("bad expression");
    CalcParser p(expr);
    double v = p.expr();
    p.ws();
    if (p.i != expr.size()) throw std::runtime_error("syntax error");
    if (v == std::floor(v) && std::fabs(v) < 1e15) return fmt("%lld", static_cast<long long>(v));
    std::string s = fmt("%.10f", v);
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (s.back() == '.') s += '0';
    return s;
}

static std::string g_calc_expr;
static TextView g_calc_disp, g_calc_hist_tv;
static std::vector<std::string> g_calc_hist;

static void calc_press(const std::string& k) {
    if (k == "C") {
        g_calc_expr.clear();
    } else if (k == "DEL") {
        if (!g_calc_expr.empty()) g_calc_expr.pop_back();
    } else if (k == "=") {
        try {
            std::string res = calc_eval(g_calc_expr);
            g_calc_hist.push_back(g_calc_expr + " = " + res);
            while (g_calc_hist.size() > 5) g_calc_hist.erase(g_calc_hist.begin());
            std::string h;
            for (size_t i = 0; i < g_calc_hist.size(); ++i) h += (i ? "\n" : "") + g_calc_hist[i];
            set_text(g_calc_hist_tv, h);
            g_calc_expr = res;
        } catch (const std::exception&) {
            g_calc_expr.clear();
            set_text(g_calc_disp, "Error");
            return;
        }
    } else {
        g_calc_expr += k;
    }
    set_text(g_calc_disp, g_calc_expr.empty() ? "0" : g_calc_expr);
}

static void build_calc() {
    LinearLayout c = new_page("calc", false);
    g_calc_hist_tv = label("", 12.0f, "sub", 5);
    add(c, g_calc_hist_tv, -1, dp(80));
    g_calc_disp = label("0", 38.0f, "text", 5);
    add(c, g_calc_disp, -1, -2, -1.0f, 0, 0, 0, 8);
    const std::vector<std::vector<std::string>> rows = {
            {"C", "(", ")", "DEL"}, {"7", "8", "9", "/"}, {"4", "5", "6", "*"}, {"1", "2", "3", "-"}, {"0", ".", "=", "+"}};
    for (auto& r : rows) {
        LinearLayout row = hbox();
        for (auto& k : r) {
            uint32_t col = (k == "C" || k == "DEL") ? DANGER
                                                    : (k == "=") ? ACCENT
                                                                 : (k == "/" || k == "*" || k == "-" || k == "+" || k == "(" || k == ")") ? ACCENT2
                                                                                                                                          : 0xFF455A64;
            add(row, button(k, [k]() { guard([&] { calc_press(k); }); }, col, 20.0f), 0, -1, 1.0f, 3, 3, 3, 3);
        }
        add(c, row, -1, 0, 1.0f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. PAINT (custom canvas view)
// ─────────────────────────────────────────────────────────────────────────────
struct Stroke {
    int32_t c;
    float w;
    std::vector<float> segs;  // x1,y1,x2,y2 ...
    float px, py;
};

class DrawView : public CustomCanvasView {
public:
    std::vector<Stroke> strokes;
    int32_t color = C(0xFFFFFFFF);
    float brush = 8.0f;
    Paint line, dot;
    View self;

    DrawView() {
        line = Paint::create();                       // NOT Paint(): that is a NULL object
        line.setAntiAlias(true);
        line.setStyle(Paint_Style::sf_get_STROKE());
        dot = Paint::create();
        dot.setAntiAlias(true);
        dot.setStyle(Paint_Style::sf_get_FILL());
        self = unchecked_as<View>();
    }

    void redraw() { if (self) self.invalidate(); }

    void on_draw(Object canvas_obj) override {
        guard([&] {
            Canvas canvas = canvas_obj.unchecked_as<Canvas>();
            canvas.drawColor(C(0xFF0B0F14));
            for (auto& s : strokes) {
                if (!s.segs.empty()) {
                    line.setColor(s.c);
                    line.setStrokeWidth(s.w);
                    canvas.drawLines(s.segs, line);   // one JNI call per stroke
                } else {
                    dot.setColor(s.c);
                    canvas.drawCircle(s.px, s.py, s.w / 2.0f, dot);
                }
            }
        });
    }

    bool on_touch_event(Object ev_obj) override {
        bool handled = true;
        guard([&] {
            MotionEvent ev = ev_obj.unchecked_as<MotionEvent>();
            int a = ev.getAction();
            float x = ev.getX(), y = ev.getY();
            if (a == 0) {
                strokes.push_back({color, brush, {}, x, y});
            } else if (a == 2 && !strokes.empty()) {
                Stroke& s = strokes.back();
                if (std::fabs(x - s.px) + std::fabs(y - s.py) > 4.0f) {
                    s.segs.insert(s.segs.end(), {s.px, s.py, x, y});
                    s.px = x;
                    s.py = y;
                }
            }
            redraw();
        });
        return handled;
    }
};

static std::unique_ptr<DrawView> g_draw;

static void build_paint() {
    LinearLayout c = new_page("paint", false);
    g_draw = std::make_unique<DrawView>();
    add(c, g_draw->self, -1, 0, 1.0f);

    LinearLayout pal = hbox();
    for (uint32_t col : {0xFFFFFFFFu, 0xFFFF5252u, 0xFFFFB300u, 0xFF69F0AEu, 0xFF40C4FFu, 0xFFE040FBu}) {
        add(pal, button(" ", [col]() { if (g_draw) g_draw->color = C(col); }, col), 0, dp(44), 1.0f, 3, 6, 3, 3);
    }
    add(c, pal);

    auto size = [](float delta) {
        if (!g_draw) return;
        g_draw->brush = std::max(2.0f, std::min(48.0f, g_draw->brush + delta));
        toast(fmt("Brush %d", static_cast<int>(g_draw->brush)));
    };
    LinearLayout row = hbox();
    add(row, button("Undo", []() {
        if (g_draw && !g_draw->strokes.empty()) { g_draw->strokes.pop_back(); g_draw->redraw(); }
    }, ACCENT2, 13.0f), 0, -2, 1.0f, 3, 3, 3, 3);
    add(row, button("Clear", []() {
        if (g_draw) { g_draw->strokes.clear(); g_draw->redraw(); }
    }, DANGER, 13.0f), 0, -2, 1.0f, 3, 3, 3, 3);
    add(row, button("Size -", [size]() { size(-3.0f); }, NAVOFF, 13.0f), 0, -2, 1.0f, 3, 3, 3, 3);
    add(row, button("Size +", [size]() { size(3.0f); }, NAVOFF, 13.0f), 0, -2, 1.0f, 3, 3, 3, 3);
    add(c, row);
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. SENSORS (level bubble + shake counter)
// ─────────────────────────────────────────────────────────────────────────────
class BubbleView : public CustomCanvasView {
public:
    float ax = 0.0f, ay = 0.0f;
    Paint ring, fill;
    View self;

    BubbleView() {
        ring = Paint::create();
        ring.setAntiAlias(true);
        ring.setStyle(Paint_Style::sf_get_STROKE());
        ring.setStrokeWidth(3.0f);
        ring.setColor(C(0xFF2E7D6F));
        fill = Paint::create();
        fill.setAntiAlias(true);
        fill.setStyle(Paint_Style::sf_get_FILL());
        self = unchecked_as<View>();
    }

    void set_tilt(float x, float y) {
        ax += (x - ax) * 0.25f;   // low-pass filter
        ay += (y - ay) * 0.25f;
        if (self) self.invalidate();
    }

    void on_draw(Object canvas_obj) override {
        guard([&] {
            Canvas canvas = canvas_obj.unchecked_as<Canvas>();
            float w = static_cast<float>(self.getWidth()), h = static_cast<float>(self.getHeight());
            float cx = w / 2.0f, cy = h / 2.0f;
            float R = std::min(w, h) * 0.42f;
            canvas.drawColor(C(0xFF0B0F14));
            canvas.drawCircle(cx, cy, R, ring);
            canvas.drawCircle(cx, cy, R / 2.0f, ring);
            canvas.drawLine(cx - R, cy, cx + R, cy, ring);
            canvas.drawLine(cx, cy - R, cx, cy + R, ring);
            float k = R / 9.81f;
            float bx = -ax * k, by = ay * k;
            float dist = std::hypot(bx, by);
            float limit = R * 0.88f;
            if (dist > limit) { bx = bx * limit / dist; by = by * limit / dist; dist = limit; }
            fill.setColor(C(dist < R * 0.08f ? 0xFF69F0AE : 0xFF00E5FF));
            canvas.drawCircle(cx + bx, cy + by, R * 0.12f, fill);
        });
    }
};

static std::unique_ptr<BubbleView> g_bubble;
static TextView g_sens_tv, g_shake_tv;
static bool g_sensors_started = false;
static int g_shakes = 0;
static double g_last_shake = 0.0;

static double now_sec() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void on_sensor(Object raw) {
    if (g_current_page != "sensors" || !g_bubble) return;
    SensorEvent ev = raw.unchecked_as<SensorEvent>();
    std::vector<float> v = ev.f_get_values();
    if (v.size() < 3) return;
    float x = v[0], y = v[1], z = v[2];
    g_bubble->set_tilt(x, y);
    set_text(g_sens_tv, fmt("X %+6.2f   Y %+6.2f   Z %+6.2f  m/s2", x, y, z));
    float mag = std::sqrt(x * x + y * y + z * z);
    double now = now_sec();
    if (mag > 18.0f && now - g_last_shake > 0.5) {
        ++g_shakes;
        g_last_shake = now;
        set_text(g_shake_tv, fmt("Shakes detected: %d", g_shakes));
    }
}

static void start_sensors() {
    if (g_sensors_started) return;
    SensorManager sm = g_act.getSystemService(JStr("sensor")).unchecked_as<SensorManager>();
    Object acc_obj = sm.getDefaultSensor(1);    // TYPE_ACCELEROMETER
    if (!acc_obj) { set_text(g_sens_tv, "No accelerometer on this device"); return; }
    Sensor acc = acc_obj.unchecked_as<Sensor>();
    sm.registerListener(Handlers()
                                .on("onSensorChanged", [](Object e) { guard([&] { on_sensor(e); }); })
                                .on("onAccuracyChanged", [](Object, int32_t) {}),
                        acc, 2);                // SENSOR_DELAY_UI
    g_sensors_started = true;
}

static void build_sensors() {
    LinearLayout c = new_page("sensors", false);
    g_bubble = std::make_unique<BubbleView>();
    add(c, g_bubble->self, -1, 0, 1.0f);
    g_sens_tv = label("Waiting for sensor...", 14.0f, "text", 17);
    add(c, g_sens_tv, -1, -2, -1.0f, 0, 8, 0, 0);
    g_shake_tv = label("Shakes detected: 0", 13.0f, "sub", 17);
    add(c, g_shake_tv);
    add(c, button("Reset shakes", []() {
        g_shakes = 0;
        set_text(g_shake_tv, "Shakes detected: 0");
    }, NAVOFF), -1, -2, -1.0f, 0, 6, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. STOPWATCH (background thread -> UI thread)
// ─────────────────────────────────────────────────────────────────────────────
static std::atomic<bool> g_sw_running{false};
static std::atomic<double> g_sw_acc{0.0}, g_sw_t0{0.0};
static TextView g_sw_label, g_laps_tv;
static std::vector<std::string> g_laps;

static std::string fmt_time(double sec) {
    int m = static_cast<int>(sec / 60.0);
    double s = sec - m * 60.0;
    return fmt("%02d:%05.2f", m, s);
}

static double sw_elapsed() {
    return g_sw_acc.load() + (g_sw_running.load() ? now_sec() - g_sw_t0.load() : 0.0);
}

static void sw_start() {
    if (g_sw_running) return;
    g_sw_t0 = now_sec();
    g_sw_running = true;
    std::thread([]() {
        while (g_sw_running) {
            std::string txt = fmt_time(sw_elapsed());
            try {
                stratum::run_on_ui_thread([txt]() {
                    if (g_sw_running) guard([&] { set_text(g_sw_label, txt); });   // ignore stale posts after Stop
                });
            } catch (...) { break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }).detach();
}

static void sw_stop() {
    if (!g_sw_running) return;
    g_sw_acc = sw_elapsed();
    g_sw_running = false;
    set_text(g_sw_label, fmt_time(g_sw_acc.load()));
}

static void sw_lap() {
    if (g_sw_running || g_sw_acc.load() > 0.0) {
        g_laps.push_back(fmt("Lap %d   %s", static_cast<int>(g_laps.size()) + 1, fmt_time(sw_elapsed()).c_str()));
        std::string out;
        size_t start = g_laps.size() > 12 ? g_laps.size() - 12 : 0;
        for (size_t i = g_laps.size(); i-- > start;) out += g_laps[i] + (i > start ? "\n" : "");
        set_text(g_laps_tv, out);
    }
}

static void sw_reset() {
    g_sw_running = false;
    g_sw_acc = 0.0;
    g_laps.clear();
    set_text(g_sw_label, "00:00.00");
    set_text(g_laps_tv, "");
}

static void build_timer() {
    LinearLayout c = new_page("timer");
    g_sw_label = label("00:00.00", 56.0f, "text", 17);
    add(c, g_sw_label, -1, -2, -1.0f, 0, 20, 0, 20);
    LinearLayout row = hbox();
    add(row, button("Start", []() { guard(sw_start); }, ACCENT), 0, -2, 1.0f, 3, 3, 3, 3);
    add(row, button("Stop", []() { guard(sw_stop); }, DANGER), 0, -2, 1.0f, 3, 3, 3, 3);
    add(row, button("Lap", []() { guard(sw_lap); }, ACCENT2), 0, -2, 1.0f, 3, 3, 3, 3);
    add(row, button("Reset", []() { guard(sw_reset); }, NAVOFF), 0, -2, 1.0f, 3, 3, 3, 3);
    add(c, row);
    g_laps_tv = label("", 15.0f, "sub");
    add(c, g_laps_tv, -1, -2, -1.0f, 0, 14, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. NOTES (persisted)
// ─────────────────────────────────────────────────────────────────────────────
static EditText g_notes_et;
static TextView g_notes_status;

static void load_notes() {
    std::ifstream f(notes_path());
    std::stringstream ss;
    if (f) ss << f.rdbuf();
    g_notes_text = ss.str();
}

static void save_notes() {
    g_notes_text = g_notes_et.getText().to_string();
    std::ofstream f(notes_path(), std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write notes file");
    f << g_notes_text;
    char tbuf[16];
    std::time_t t = std::time(nullptr);
    std::tm tmv;
    localtime_r(&t, &tmv);
    std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &tmv);
    set_text(g_notes_status, fmt("Saved: %d chars, %d words  (%s)", static_cast<int>(utf8_len(g_notes_text)),
                                 static_cast<int>(word_count(g_notes_text)), tbuf));
}

static void build_notes() {
    LinearLayout c = new_page("notes");
    g_notes_et = edit("Write something...", 12);
    set_text(g_notes_et, g_notes_text);
    add(c, g_notes_et);
    LinearLayout row = hbox();
    add(row, button("Save", []() { guard(save_notes); }), 0, -2, 1.0f, 0, 6, 4, 6);
    add(row, button("Clear", []() { set_text(g_notes_et, ""); }, DANGER), 0, -2, 1.0f, 4, 6, 0, 6);
    add(c, row);
    g_notes_status = label("", 12.0f, "sub");
    add(c, g_notes_status);
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. BENCHMARK (UI thread, one test per post, time-budgeted)
// ─────────────────────────────────────────────────────────────────────────────
static TextView g_bench_out;
static Button g_bench_btn;
static std::vector<std::string> g_bench_lines;
static size_t g_bench_idx = 0;
static constexpr double BENCH_BUDGET = 1.5;

struct BenchResult { double ops; double us; std::string detail; };

template <class F>
static std::pair<long long, double> bench_loop(F body, long long iters) {
    body();  // warm-up: resolves class/method slots
    long long n = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (n < iters) {
        long long chunk = std::min<long long>(50, iters - n);
        for (long long i = 0; i < chunk; ++i) body();
        n += chunk;
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > BENCH_BUDGET) break;
    }
    return {n, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()};
}

static BenchResult make_result(long long n, double el, const std::string& detail) {
    return {n / el, (el / n) * 1e6, detail};
}

static BenchResult bt_static() {
    auto r = bench_loop([] { JInteger::compare(3, 30000); }, 60000);
    return make_result(r.first, r.second, commas(r.first) + " calls");
}
static BenchResult bt_instance() {
    JInteger obj(12345);
    auto r = bench_loop([&] { obj.intValue(); }, 60000);
    return make_result(r.first, r.second, commas(r.first) + " calls");
}
static BenchResult bt_string() {
    auto r = bench_loop([] { JString("Stratum_Test_String_123").length(); }, 30000);
    return make_result(r.first, r.second, commas(r.first) + " strings");
}
static BenchResult bt_int_array() {
    std::vector<int32_t> base;
    for (int i = 150; i > 0; --i) base.push_back(i);
    auto r = bench_loop([&] { std::vector<int32_t> tmp = base; Arrays::sort(tmp); }, 1500);
    return make_result(r.first, r.second, commas(r.first) + " arrays (150 ints each)");
}
static BenchResult bt_byte_array() {
    std::vector<uint8_t> payload(32 * 1024, 'A');
    auto r = bench_loop([&] {
        JString j(payload, "ISO-8859-1");
        j.getBytes("ISO-8859-1");
    }, 1000);
    double mbs = (r.first * 32.0 * 2.0) / 1024.0 / r.second;
    return make_result(r.first, r.second, fmt("%.1f MB/s throughput", mbs));
}
static BenchResult bt_dict() {
    auto r = bench_loop([] {
        Object values = stratum::new_list({stratum::box_i32(10), stratum::box_i32(20), stratum::box_i32(30)});
        Object m = stratum::new_map({
                                            {stratum::java_string("user"), stratum::java_string("Android")},
                                            {stratum::java_string("values"), values},
                                            {stratum::java_string("active"), stratum::box_bool(true)},
                                            {stratum::java_string("score"), stratum::box_f64(99.5)}});
        auto back = m.as_map();
        (void)back;
    }, 1000);
    return make_result(r.first, r.second, commas(r.first) + " conversions");
}
static BenchResult bt_gc() {
    auto r = bench_loop([] { JInteger(7).intValue(); }, 15000);
    return make_result(r.first, r.second, commas(r.first) + " objects freed without crash");
}
// NEW: building a 2-D Java array from C++ (3 JNI arrays per iteration: 2 rows + the outer float[][]).
static BenchResult bt_new_array() {
    auto r = bench_loop([] {
        Object m = stratum::new_array({{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}});
        (void)m;
    }, 3000);
    return make_result(r.first, r.second, commas(r.first) + " float[2][3] arrays built");
}
// NEW: direct call with an Object[] ('A') parameter: Arrays.deepToString(Object[]).
static BenchResult bt_obj_array_call() {
    std::vector<Object> rows{stratum::new_array({1, 2}), stratum::new_array({3, 4})};
    stratum::ObjArr oa(rows);
    auto r = bench_loop([&] { Arrays::deepToString(oa); }, 3000);
    return make_result(r.first, r.second, commas(r.first) + " Object[] calls");
}

static const std::vector<std::pair<std::string, std::function<BenchResult()>>>& bench_tests() {
    static const std::vector<std::pair<std::string, std::function<BenchResult()>>> T = {
            {"Static Method Dispatch", bt_static},
            {"Instance Method Call", bt_instance},
            {"String Marshalling", bt_string},
            {"int[] Array Marshalling", bt_int_array},
            {"byte[] Bridge Throughput", bt_byte_array},
            {"dict <-> HashMap Bridge", bt_dict},
            {"JNI GlobalRef / Churn", bt_gc},
            {"new_array float[][] build", bt_new_array},
            {"Object[] ('A') param call", bt_obj_array_call}};
    return T;
}

static void bench_refresh() {
    std::string s;
    for (size_t i = 0; i < g_bench_lines.size(); ++i) s += (i ? "\n" : "") + g_bench_lines[i];
    set_text(g_bench_out, s);
}

static void bench_step();

static void bench_post(void (*fn)()) {
    g_bench_btn.post([fn]() { guard(fn); });
}

static void bench_run_single() {
    const auto& tests = bench_tests();
    const size_t num = g_bench_idx + 1, total = tests.size();
    try {
        BenchResult r = tests[g_bench_idx].second();
        g_bench_lines.back() = fmt("[%d/%d] %s: PASS\n   -> %s ops/sec (%.2f us/call) | %s", (int)num, (int)total,
                                   tests[g_bench_idx].first.c_str(), commas((long long)r.ops).c_str(), r.us,
                                   r.detail.c_str());
    } catch (const std::exception& e) {
        g_bench_lines.back() = fmt("[%d/%d] %s: FAILED (%s)", (int)num, (int)total,
                                   tests[g_bench_idx].first.c_str(), e.what());
    }
    bench_refresh();
    ++g_bench_idx;
    bench_post(bench_step);
}

static void bench_step() {
    const auto& tests = bench_tests();
    if (g_bench_idx >= tests.size()) {
        g_bench_lines.push_back(std::string(34, '='));
        g_bench_lines.push_back("ALL BENCHMARKS FINISHED");
        bench_refresh();
        g_bench_btn.setEnabled(true);
        set_text(g_bench_btn, "RUN AGAIN");
        return;
    }
    g_bench_lines.push_back(fmt("[%d/%d] %s: RUNNING...", (int)g_bench_idx + 1, (int)tests.size(),
                                tests[g_bench_idx].first.c_str()));
    bench_refresh();
    bench_post(bench_run_single);  // yield so "RUNNING..." paints before the test blocks the UI
}

static void run_bench() {
    g_bench_btn.setEnabled(false);
    set_text(g_bench_btn, "TESTING...");
    g_bench_lines.clear();
    g_bench_lines.push_back("Starting Benchmark Suite...");
    g_bench_lines.push_back(std::string(34, '='));
    bench_refresh();
    g_bench_idx = 0;
    bench_post(bench_step);
}

static void copy_to_clipboard(const std::string& title, const std::string& text) {
    ClipboardManager cm = g_act.getSystemService(JStr("clipboard")).unchecked_as<ClipboardManager>();
    cm.setPrimaryClip(ClipData::newPlainText(JStr(title), JStr(text)));
}

static void copy_bench() {
    if (g_bench_lines.empty()) { toast("Run the benchmark first"); return; }
    std::string report = "=== DEVICE HARDWARE SPECS ===\n" + specs_text() + "\n\n=== STRATUM BENCHMARK RESULTS ===\n";
    for (size_t i = 0; i < g_bench_lines.size(); ++i) report += (i ? "\n" : "") + g_bench_lines[i];
    copy_to_clipboard("Stratum Report", report);
    toast("Report copied");
}

static void build_bench() {
    LinearLayout c = new_page("bench");
    add(c, label("Measures C++ -> JNI -> Java round-trips.", 13.0f, "sub"), -1, -2, -1.0f, 0, 0, 0, 8);
    LinearLayout row = hbox();
    g_bench_btn = button("START BENCHMARK", []() { guard(run_bench); }, ACCENT2);
    add(row, g_bench_btn, 0, -2, 1.0f, 0, 4, 4, 4);
    add(row, button("COPY REPORT", []() { guard(copy_bench); }, NAVOFF), 0, -2, 1.0f, 4, 4, 0, 4);
    add(c, row);
    g_bench_out = label("Press START BENCHMARK.\nResults appear here in real time.", 12.0f);
    add(c, g_bench_out, -1, -2, -1.0f, 0, 12, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 8b. ARRAYS (NEW): typed + multi-dimensional Java arrays, reflect vectors, safety
// ─────────────────────────────────────────────────────────────────────────────
static TextView g_arr_out;
static std::string g_arr_report;

template <class T>
static std::string vec_str(const std::vector<T>& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ", ";
        s += std::to_string(v[i]);
    }
    return s + "]";
}

// Java's own view of an array, via reflect (proves the engine built the right Java type).
static std::string jflat(const Object& o) {      // primitive 1-D arrays: Arrays.toString(int[]) etc.
    return stratum::reflect::call_static<std::string>("java.util.Arrays", "toString", o);
}
static std::string jdeep(const Object& o) {      // Object[] / multi-dim arrays
    return stratum::reflect::call_static<std::string>("java.util.Arrays", "deepToString", o);
}
static std::string jclassname(const Object& o) { // e.g. "class [[F"
    Object c = stratum::reflect::call_method<Object>(o, "getClass");
    return c.to_string();
}

static void run_array_tests() {
    std::string out;
    int total = 0, failed = 0;
    auto section = [&](const char* name, auto&& fn) {
        ++total;
        out += std::string("[") + name + "]\n   ";
        try {
            out += fn();
        } catch (const std::exception& e) {
            ++failed;
            out += std::string("FAILED: ") + e.what();
        }
        out += "\n";
    };

    out += fmt("sizeof(void*)=%d  sizeof(long)=%d  (new_array picks the Java type by size, so this is safe on 32/64-bit)\n\n",
               (int)sizeof(void*), (int)sizeof(long));

    section("1-D int[] (braced)", [&]() -> std::string {
        Object a = stratum::new_array({1, 2, 3});
        return jflat(a) + "   back in C++: " + vec_str(a.as_array<int32_t>()) + "   " + jclassname(a);
    });

    section("2-D float[][] (braced)", [&]() -> std::string {
        Object m = stratum::new_array({{1.5f, 2.5f}, {3.5f, 4.5f}});
        std::vector<Object> rows = m.as_array<Object>();
        return jdeep(m) + "   " + jclassname(m) + "   row1 back in C++: " + vec_str(rows.at(1).as_array<float>());
    });

    section("3-D int[][][] (braced)", [&]() -> std::string {
        Object c = stratum::new_array({{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}});
        return jdeep(c) + "   " + jclassname(c);
    });

    section("String[] and String[][]", [&]() -> std::string {
        Object s1 = stratum::new_array({"alpha", "beta"});
        Object s2 = stratum::new_array({{"a", "b"}, {"c"}});
        return jdeep(s1) + " " + jclassname(s1) + "\n   " + jdeep(s2) + " " + jclassname(s2);
    });

    section("boolean[] / long[] / short[]", [&]() -> std::string {
        Object z = stratum::new_array({true, false, true});
        Object j = stratum::new_array<long long>({10000000000LL, 2LL});   // 64-bit Java long on every ABI
        Object sh = stratum::new_array<int16_t>({1, -2});
        std::vector<bool> zb = z.as_array<bool>();
        std::string zs;
        for (bool b : zb) zs += b ? "T" : "F";
        return jflat(z) + " " + jclassname(z) + " (C++: " + zs + ")\n   " + jflat(j) + " " + jclassname(j) +
               "\n   " + jflat(sh) + " " + jclassname(sh);
    });

    section("nested std::vector<std::vector<double>>", [&]() -> std::string {
        std::vector<std::vector<double>> g{{1.0, 2.0}, {3.0}};
        Object o = stratum::new_array(g);                                  // jagged double[][]
        return jdeep(o) + "   " + jclassname(o);
    });

    section("Integer[] from std::vector<JInteger>", [&]() -> std::string {
        std::vector<JInteger> ints;
        ints.push_back(JInteger(7));
        ints.push_back(JInteger(9));
        Object ia = stratum::new_array(ints);                              // real java.lang.Integer[]
        return jdeep(ia) + "   " + jclassname(ia);
    });

    section("direct call: Object[] ('A') param, NAMED ObjArr", [&]() -> std::string {
        std::vector<Object> rows{stratum::new_array({1, 2}), stratum::new_array({3, 4})};
        stratum::ObjArr named(rows);                                       // before the update: compile error (deleted copy)
        return Arrays::deepToString(named);
    });

#if STRATUM_DEMO_DIRECT_JOIN
    section("direct call: String.join(CharSequence, CharSequence[])  (typed 'A' param)", [&]() -> std::string {
        std::vector<Object> parts{stratum::java_string("x"), stratum::java_string("y"), stratum::java_string("z")};
        stratum::ObjArr oa(parts);
        return JString::join(JStr(", "), oa);                              // engine builds a real CharSequence[]
    });
#endif

    section("reflect + std::vector (String.join, Arrays.toString, 2-D)", [&]() -> std::string {
        std::vector<std::string> names{"x", "y"};
        std::string a = stratum::reflect::call_static<std::string>("java.lang.String", "join", ", ", names);
        std::string b = stratum::reflect::call_static<std::string>("java.util.Arrays", "toString", std::vector<int32_t>{5, 6});
        std::vector<std::vector<int32_t>> grid{{1, 2}, {3, 4}};
        std::string c = stratum::reflect::call_static<std::string>("java.util.Arrays", "deepToString", grid);
        return a + "  |  " + b + "  |  " + c;
    });

    section("wrong element type -> clean error (no crash)", [&]() -> std::string {
        try {
            stratum::new_object_array("java/lang/String", {stratum::box_i32(1)});
            throw std::runtime_error("expected an ArrayStoreException but none was thrown");
        } catch (const stratum::JavaException& e) {
            return std::string("caught: ") + e.what();
        }
    });

    out += fmt("\n%d/%d sections passed", total - failed, total);
    g_arr_report = out;
    set_text(g_arr_out, out);
}

static void copy_arrays() {
    if (g_arr_report.empty()) { toast("Run the array tests first"); return; }
    copy_to_clipboard("Stratum arrays", "=== DEVICE ===\n" + specs_text() + "\n\n=== ARRAY TESTS ===\n" + g_arr_report);
    toast("Array report copied");
}

static void build_arrays() {
    LinearLayout c = new_page("arrays");
    add(c, label("Typed & multi-dimensional Java arrays built from C++.", 13.0f, "sub"), -1, -2, -1.0f, 0, 0, 0, 8);
    LinearLayout row = hbox();
    add(row, button("RUN ARRAY TESTS", []() { guard(run_array_tests); }, ACCENT2), 0, -2, 1.0f, 0, 4, 4, 4);
    add(row, button("COPY REPORT", []() { guard(copy_arrays); }, NAVOFF), 0, -2, 1.0f, 4, 4, 0, 4);
    add(c, row);
    g_arr_out = label("Press RUN ARRAY TESTS.\nEach section prints what Java itself sees.", 12.0f);
    add(c, g_arr_out, -1, -2, -1.0f, 0, 12, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. SETTINGS
// ─────────────────────────────────────────────────────────────────────────────
static TextView g_size_preview;

static void apply_theme(bool dark) {
    g_theme = dark ? DARK_T : LIGHT_T;
    for (auto& p : g_themed_views) paint_view_role(p.first, p.second);
    for (auto& p : g_themed_texts) paint_text_role(p.first, p.second);
    render_todos();
}

static void reset_data() {
    g_todos.clear();
    g_notes_text.clear();
    std::remove(todo_path().c_str());
    std::remove(notes_path().c_str());
    set_text(g_notes_et, "");
    render_todos();
    toast("All data cleared");
}

static void on_size(int progress) {
    float px = 10.0f + static_cast<float>(progress);
    g_size_preview.setTextSize(px);
    g_notes_et.setTextSize(px);
}

static void build_settings() {
    LinearLayout c = new_page("settings");

    LinearLayout a = card(c);
    add(a, label("Appearance", 13.0f, "sub"));
    CheckBox cb(g_act);
    set_text(cb, "Dark mode");
    cb.setTextSize(16.0f);
    themed_text(cb.unchecked_as<TextView>(), "text");
    cb.setChecked(true);
    cb.setOnCheckedChangeListener([](Object, bool checked) { guard([&] { apply_theme(checked); }); });
    add(a, cb);

    LinearLayout b = card(c);
    add(b, label("Text size (also applies to Notes)", 13.0f, "sub"));
    g_size_preview = label("The quick brown fox", 14.0f);
    add(b, g_size_preview, -1, -2, -1.0f, 0, 4, 0, 4);
    SeekBar sb(g_act);
    sb.setMax(20);
    sb.setProgress(4);
    sb.setOnSeekBarChangeListener(Handlers()
                                          .on("onProgressChanged", [](Object, int32_t p, bool) { guard([&] { on_size(p); }); })
                                          .on("onStartTrackingTouch", [](Object) {})
                                          .on("onStopTrackingTouch", [](Object) {}));
    add(b, sb);

    LinearLayout d = card(c);
    add(d, label("Tools", 13.0f, "sub"));
    add(d, button("Show a Toast", []() { toast("Hello from C++!"); }, ACCENT2), -1, -2, -1.0f, 0, 4, 0, 4);
    add(d, button("Copy device info", []() {
        guard([] {
            copy_to_clipboard("Stratum", "Stratum Demo\n" + specs_text());
            toast("Device info copied");
        });
    }, ACCENT), -1, -2, -1.0f, 0, 4, 0, 4);
    add(d, button("Reset all data", []() { guard(reset_data); }, DANGER), -1, -2, -1.0f, 0, 4, 0, 4);

    LinearLayout e = card(c);
    add(e, label("About", 13.0f, "sub"));
    add(e, label("Stratum Demo\nNative C++ UI over the Android SDK.\n" + specs_text(), 13.0f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle entry points (auto-discovered by the SDK)
// ─────────────────────────────────────────────────────────────────────────────
static void reset_state() {
    g_sw_running = false;
    g_themed_views.clear();
    g_themed_texts.clear();
    g_pages.clear();
    g_nav_btns.clear();
    g_draw.reset();
    g_bubble.reset();
    g_sensors_started = false;
    g_title_tv = TextView();
    g_content_frame = FrameLayout();
    g_home_date = TextView(); g_home_stats = TextView();
    g_todo_list = LinearLayout(); g_todo_count = TextView(); g_todo_input = EditText();
    g_calc_disp = TextView(); g_calc_hist_tv = TextView();
    g_sens_tv = TextView(); g_shake_tv = TextView();
    g_sw_label = TextView(); g_laps_tv = TextView();
    g_notes_et = EditText(); g_notes_status = TextView();
    g_bench_out = TextView(); g_bench_btn = Button();
    g_arr_out = TextView(); g_arr_report.clear();
    g_size_preview = TextView();
}

template <class F>
static void build_safely(const char* name, F&& f) {
    try { f(); }
    catch (const std::exception& e) { logE((std::string("build ") + name + " failed: " + e.what()).c_str()); }
    catch (...) { logE((std::string("build ") + name + " failed").c_str()); }
}

void onCreate(Activity act) {
    reset_state();
    g_act = act;
    g_theme = DARK_T;
    try {
        g_density = g_act.getResources().unchecked_as<stratum::android::content::res::Resources>()
                .getDisplayMetrics().unchecked_as<stratum::android::util::DisplayMetrics>()
                .f_get_density();
    } catch (...) {}
    load_specs();
    load_todos();
    load_notes();

    LinearLayout root = vbox("bg");
    g_title_tv = TextView(g_act);
    g_title_tv.setTextSize(20.0f);
    g_title_tv.setTextColor(C(0xFFFFFFFF));
    g_title_tv.setBackgroundColor(C(0xFF00796B));
    g_title_tv.setPadding(dp(16), dp(44), dp(16), dp(14));
    add(root, g_title_tv);

    g_content_frame = FrameLayout(g_act);
    add(root, g_content_frame, -1, 0, 1.0f);

    build_safely("home", build_home);
    build_safely("todo", build_todo);
    build_safely("calc", build_calc);
    build_safely("paint", build_paint);
    build_safely("sensors", build_sensors);
    build_safely("timer", build_timer);
    build_safely("notes", build_notes);
    build_safely("bench", build_bench);
    build_safely("arrays", build_arrays);
    build_safely("settings", build_settings);

    LinearLayout nav = hbox("card", 4);
    for (auto& item : NAV) {
        const std::string name = item.first;
        Button b = button(item.second, [name]() { guard([&] { show_page(name); }); }, NAVOFF, 12.0f);
        g_nav_btns[name] = b;
        add(nav, b, dp(84), -2, -1.0f, 2, 0, 2, 0);
    }
    HorizontalScrollView hs(g_act);
    hs.addView(nav);
    themed_view(hs.unchecked_as<View>(), "card");
    add(root, hs);

    stratum::setContentView(g_act, root);
    show_page("home");
}

bool onBackPressed() {
    if (g_current_page != "home") {
        show_page("home");
        return true;
    }
    return false;
}

void onDestroy() {
    reset_state();
}