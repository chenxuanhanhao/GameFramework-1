﻿//
// Project: cliblisp
// Created by bajdcc
//

#include "stdafx.h"
#include <regex>
#include <iostream>
#include <fstream>
#include <sstream>
#include "cgui.h"
#include "cexception.h"
#include "../../ui/gdi/Gdi.h"
#include "Parser2D.h"

#define LOG_AST 0
#define LOG_DEP 0

#define ENTRY_FILE "/sys/entry"

#define MAKE_ARGB(a,r,g,b) ((uint32_t)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)|(((DWORD)(BYTE)(a))<<24)))
#define MAKE_RGB(r,g,b) MAKE_ARGB(255,r,g,b)
#define GET_R(rgb) (LOBYTE(rgb))
#define GET_G(rgb) (LOBYTE(((WORD)(rgb)) >> 8))
#define GET_B(rgb) (LOBYTE((WORD)((rgb)>>16)))
#define GET_A(rgb) (LOBYTE((rgb)>>24))

extern int g_argc;
extern char** g_argv;

namespace clib {

    cgui::cgui() {
        buffer = memory.alloc_array<char>((uint)size);
        assert(buffer);
        memset(buffer, 0, (uint)size);
        colors_bg = memory.alloc_array<uint32_t>((uint)size);
        assert(colors_bg);
        color_bg = 0;
        std::fill(colors_bg, colors_bg + size, color_bg);
        colors_fg = memory.alloc_array<uint32_t>((uint)size);
        assert(colors_fg);
        color_fg = MAKE_RGB(255, 255, 255);
        std::fill(colors_fg, colors_fg + size, color_fg);
        color_bg_stack.push_back(color_bg);
        color_fg_stack.push_back(color_fg);
    }

    cgui& cgui::singleton() {
        static clib::cgui gui;
        return gui;
    }

    string_t cgui::load_file(string_t& name) {
        static string_t pat_path{ R"((/[A-Za-z0-9_]+)+)" };
        static std::regex re_path(pat_path);
        static string_t pat_bin{ R"([A-Za-z0-9_]+)" };
        static std::regex re_bin(pat_bin);
        std::smatch res;
        string_t path;
        if (std::regex_match(name, res, re_path)) {
            path = FILE_ROOT + res[0].str() + ".cpp";
        }
        else if (std::regex_match(name, res, re_bin)) {
            path = FILE_ROOT + ("/bin/" + res[0].str()) + ".cpp";
        }
        if (path.empty())
            error("file not exists: " + name);
        std::ifstream t(path);
        if (t) {
            std::stringstream buffer;
            buffer << t.rdbuf();
            auto str = buffer.str();
            // UTF-8 to GBK
            {
                int len = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)str.c_str(), -1, NULL, 0);
                wchar_t* wszGBK = new wchar_t[len];
                memset(wszGBK, 0, len);
                MultiByteToWideChar(CP_UTF8, 0, (LPCCH)str.c_str(), -1, wszGBK, len);

                len = WideCharToMultiByte(CP_ACP, 0, wszGBK, -1, NULL, 0, NULL, NULL);
                char* szGBK = new char[len + 1];
                memset(szGBK, 0, len + 1);
                WideCharToMultiByte(CP_ACP, 0, wszGBK, -1, szGBK, len, NULL, NULL);

                str = szGBK;
                delete[] szGBK;
                delete[] wszGBK;
            }
            std::vector<byte> data(str.begin(), str.end());
            vm->as_root(true);
            if (name[0] != '/')
                name = "/bin/" + name;
            vm->write_vfs(name, data);
            vm->as_root(false);
            return str;
        }
        std::vector<byte> data;
        if (vm->read_vfs(name, data)) {
            return string_t(data.begin(), data.end());
        }
        error("file not exists: " + name);
        return "";
    }

    void cgui::reset() {
        if (vm) {
            vm.reset();
            gen.reset();
            running = false;
            cvm::global_state.input_lock = -1;
            cvm::global_state.input_content.clear();
            cvm::global_state.input_waiting_list.clear();
            cvm::global_state.input_read_ptr = -1;
            cvm::global_state.input_success = false;
        }
    }

    void cgui::draw(CComPtr<ID2D1RenderTarget>& rt, const CRect& bounds, const Parser2DEngine::BrushBag& brushes, bool paused, decimal fps) {
        if (!paused) {
            if (cvm::global_state.interrupt) {
                cycle = GUI_CYCLES;
            }
            else if (cycle_set) {
                // ...
            }
            else if (cycle_stable > 0) {
                if (fps > GUI_MAX_FPS_RATE) {
                    cycle = min(cycle << 1, GUI_MAX_CYCLE);
                }
                else if (fps < GUI_MIN_FPS_RATE) {
                    cycle_stable--;
                }
            }
            else if (fps > GUI_MAX_FPS_RATE) {
                if (cycle_speed >= 0) {
                    cycle_speed = min(cycle_speed + 1, GUI_MAX_SPEED);
                    cycle = min(cycle << cycle_speed, GUI_MAX_CYCLE);
                }
                else {
                    cycle_speed = 0;
                }
            }
            else if (fps < GUI_MIN_FPS_RATE) {
                if (cycle_speed <= 0) {
                    cycle_speed = max(cycle_speed - 1, -GUI_MAX_SPEED);
                    cycle = max(cycle >> (-cycle_speed), GUI_MIN_CYCLE);
                }
                else {
                    cycle_speed = 0;
                }
            }
            else {
                if (cycle_stable == 0) {
                    cycle_speed = 0;
                    cycle_stable = GUI_CYCLE_STABLE;
                }
            }
            for (int i = 0; i < ticks; ++i) {
                tick();
            }
        }
        draw_text(rt, bounds, brushes);
    }

    void cgui::draw_text(CComPtr<ID2D1RenderTarget>& rt, const CRect& bounds, const Parser2DEngine::BrushBag& brushes) {
        int w = bounds.Width();
        int h = bounds.Height();
        int width = cols * GUI_FONT_W;
        int height = rows * GUI_FONT_H;

        int x = max((w - width) / 2, 0);
        int y = max((h - height) / 2, 0);
        auto old_x = x;
        char c;

        CComPtr<ID2D1SolidColorBrush> b;
        TCHAR s[2] = { 0 };
        char sc[3] = { 0 };
        bool ascii = true;
        bool ascii_head = false;

        for (auto i = 0; i < rows; ++i) {
            for (auto j = 0; j < cols; ++j) {
                static WORD wd;
                ascii = true;
                c = buffer[i * cols + j];
                if (c < 0) {
                    wd = (((BYTE)c) << 8) | ((BYTE)buffer[i * cols + j + 1]);
                    if (wd >= 0x8140 && wd <= 0xFEFE) { // GBK
                        if (j < cols - 1)
                            ascii = false;
                        else
                            ascii_head = true;
                    }
                }
                if (j == 0 && ascii_head) {
                    ascii_head = false;
                    ascii = true;
                }
                if (ascii) {
                    if (colors_bg[i * cols + j]) {
                        rt->CreateSolidColorBrush(D2D1::ColorF(colors_bg[i * cols + j]), &b);
                        rt->FillRectangle(
                            D2D1::RectF((float)bounds.left + x, (float)bounds.top + y + GUI_FONT_H_1,
                            (float)bounds.left + x + GUI_FONT_W, (float)bounds.top + y + GUI_FONT_H_2), b);
                        b.Release();
                    }
                    if (c > 0) {
                        if (std::isprint(buffer[i * cols + j])) {
                            rt->CreateSolidColorBrush(D2D1::ColorF(colors_fg[i * cols + j]), &b);
                            s[0] = c;
                            rt->DrawText(s, 1, brushes.cmdTF->textFormat,
                                D2D1::RectF((float)bounds.left + x, (float)bounds.top + y + GUI_FONT_H_1,
                                (float)bounds.left + x + GUI_FONT_W, (float)bounds.top + y + GUI_FONT_H_2), b);
                            b.Release();
                        }
                        else if (c == '\7') {
                            rt->CreateSolidColorBrush(D2D1::ColorF(colors_fg[i * cols + j]), &b);
                            rt->FillRectangle(
                                D2D1::RectF((float)bounds.left + x, (float)bounds.top + y + GUI_FONT_H_1,
                                (float)bounds.left + x + GUI_FONT_W, (float)bounds.top + y + GUI_FONT_H_2), b);
                            b.Release();
                        }
                    }
                    else if (c < 0) {
                        rt->CreateSolidColorBrush(D2D1::ColorF(colors_fg[i * cols + j]), &b);
                        rt->FillRectangle(
                            D2D1::RectF((float)bounds.left + x, (float)bounds.top + y + GUI_FONT_H_1,
                            (float)bounds.left + x + GUI_FONT_W, (float)bounds.top + y + GUI_FONT_H_2), b);
                        b.Release();
                    }
                    x += GUI_FONT_W;
                }
                else {
                    if (colors_bg[i * cols + j]) {
                        rt->CreateSolidColorBrush(D2D1::ColorF(colors_bg[i * cols + j]), &b);
                        rt->FillRectangle(
                            D2D1::RectF((float)bounds.left + x, (float)bounds.top + y + GUI_FONT_H_1,
                            (float)bounds.left + x + GUI_FONT_W * 2, (float)bounds.top + y + GUI_FONT_H_2), b);
                        b.Release();
                    }
                    sc[0] = c;
                    sc[1] = buffer[i * cols + j + 1];
                    auto utf = cnet::GBKToStringT(sc);
                    s[0] = (TCHAR)(utf[0]);
                    j++;
                    rt->CreateSolidColorBrush(D2D1::ColorF(colors_fg[i * cols + j]), &b);
                    rt->DrawText(s, 1, brushes.gbkTF->textFormat,
                        D2D1::RectF((float)bounds.left + x + GUI_FONT_W_C1, (float)bounds.top + y + GUI_FONT_H_C1,
                        (float)bounds.left + x + GUI_FONT_W_C2, (float)bounds.top + y + GUI_FONT_H_C2), b);
                    b.Release();
                    x += GUI_FONT_W * 2;
                }
            }
            x = old_x;
            y += GUI_FONT_H;
        }

        if (input_state) {
            input_ticks++;
            if (input_ticks > GUI_INPUT_CARET) {
                input_caret = !input_caret;
                input_ticks = 0;
            }
            if (input_caret) {
                if (ptr_x <= cols - 1) {
                    int _x = max((w - width) / 2, 0) + ptr_x * GUI_FONT_W;
                    int _y = max((h - height) / 2, 0) + ptr_y * GUI_FONT_H;
                    rt->CreateSolidColorBrush(D2D1::ColorF(color_fg_stack.back()), &b);
                    rt->DrawText(_T("_"), 1, brushes.cmdTF->textFormat,
                        D2D1::RectF((float)bounds.left + _x, (float)bounds.top + _y + GUI_FONT_H_1,
                        (float)bounds.left + _x + GUI_FONT_W, (float)bounds.top + _y + GUI_FONT_H_2), b);
                    b.Release();
                }
                else if (ptr_y < rows - 1) {
                    int _x = max((w - width) / 2, 0);
                    int _y = max((h - height) / 2, 0) + (ptr_y + 1) * GUI_FONT_H;
                    rt->CreateSolidColorBrush(D2D1::ColorF(color_fg_stack.back()), &b);
                    rt->DrawText(_T("_"), 1, brushes.cmdTF->textFormat,
                        D2D1::RectF((float)bounds.left + _x, (float)bounds.top + _y + GUI_FONT_H_1,
                        (float)bounds.left + _x + GUI_FONT_W, (float)bounds.top + _y + GUI_FONT_H_2), b);
                    b.Release();
                }
            }
        }
    }

    void cgui::tick() {
        if (exited)
            return;
        if (running) {
            try {
                if (!vm->run(cycle, cycles)) {
                    running = false;
                    exited = true;
                    put_string("\n[!] clibos exited.");
                    vm.reset();
                    gen.reset();
                }
            }
            catch (const cexception & e) {
                ATLTRACE("[SYSTEM] ERR  | RUNTIME ERROR: %s\n", e.message().c_str());
                vm.reset();
                gen.reset();
                running = false;
            }
        }
        else {
            if (!vm) {
                vm = std::make_unique<cvm>();
                std::vector<string_t> args;
                if (g_argc > 0) {
                    args.emplace_back(ENTRY_FILE);
                    for (int i = 1; i < g_argc; ++i) {
                        args.emplace_back(g_argv[i]);
                    }
                }
                if (compile(ENTRY_FILE, args) != -1) {
                    running = true;
                }
            }
        }
    }

    void cgui::put_string(const string_t & str) {
        for (auto& s : str) {
            put_char(s);
        }
    }

    void cgui::put_char(char c) {
        if (cmd_state) {
            if (c == '\033') {
                static string_t pat{ R"([A-Za-z][0-9a-f]{1,8})" };
                static std::regex re(pat);
                std::smatch res;
                string_t s(cmd_string.begin(), cmd_string.end());
                if (std::regex_search(s, res, re)) {
                    try {
                        exec_cmd(s);
                    }
                    catch (const std::invalid_argument&) {
                        // '/dev/random' : cause error
                    }
                }
                cmd_string.clear();
                cmd_state = false;
            }
            else {
                cmd_string.push_back(c);
            }
            return;
        }
        else if (c == '\033') {
            cmd_state = true;
            return;
        }
        if (c == 0)
            return;
        if (c == '\n') {
            ptr_x = 0;
            if (ptr_y == rows - 1) {
                new_line();
            }
            else {
                ptr_y++;
            }
        }
        else if (c == '\b') {
            if (ptr_mx + ptr_my * cols < ptr_x + ptr_y * cols) {
                forward(ptr_x, ptr_y, false);
                draw_char('\u0000');
                if (!(ptr_x == ptr_rx && ptr_y == ptr_ry)) {
                    for (auto i = ptr_y * cols + ptr_x; i < ptr_ry * cols + ptr_rx; ++i) {
                        buffer[i] = buffer[i + 1];
                        colors_bg[i] = colors_bg[i + 1];
                        colors_fg[i] = colors_fg[i + 1];
                    }
                    buffer[ptr_ry * cols + ptr_rx] = '\0';
                    colors_bg[ptr_ry * cols + ptr_rx] = color_bg;
                    colors_fg[ptr_ry * cols + ptr_rx] = color_fg;
                }
                forward(ptr_rx, ptr_ry, false);
            }
        }
        else if (c == '\u0002') {
            ptr_x--;
            while (ptr_x >= 0) {
                draw_char('\u0000');
                ptr_x--;
            }
            ptr_x = 0;
        }
        else if (c == '\r') {
            ptr_x = 0;
        }
        else if (c == '\f') {
            ptr_x = 0;
            ptr_y = 0;
            ptr_mx = 0;
            ptr_my = 0;
            ptr_rx = 0;
            ptr_ry = 0;
            memset(buffer, 0, (uint)size);
            std::fill(colors_bg, colors_bg + size, color_bg);
            std::fill(colors_fg, colors_fg + size, color_fg);
        }
        else {
            auto rx = ptr_rx == -1 ? ptr_x : ptr_rx;
            auto ry = ptr_ry == -1 ? ptr_y : ptr_ry;
            auto end = rx == cols - 1 && ry == rows - 1;
            auto nl = rx == ptr_x && rx == cols - 1;
            if (end) {
                if (nl) {
                    draw_char(c);
                    new_line();
                    ptr_x = 0;
                }
                else {
                    new_line();
                    ptr_y--;
                    draw_char(c);
                    ptr_x++;
                }
            }
            else {
                draw_char(c);
                forward(ptr_x, ptr_y, true);
            }
        }
    }

    void cgui::input_char(char c) {
        if (c > 0)
            input(c);
    }

    void cgui::new_line() {
        if (ptr_my != -1) {
            if (ptr_my == 0) {
                ptr_mx = ptr_my = 0;
            }
            else {
                ptr_my--;
            }
        }
        memcpy(buffer, buffer + cols, (uint)cols * (rows - 1));
        memset(&buffer[cols * (rows - 1)], 0, (uint)cols);
        memcpy(colors_bg, colors_bg + cols, (uint)cols * (rows - 1) * sizeof(uint32_t));
        std::fill(&colors_bg[cols * (rows - 1)], &colors_bg[cols * (rows)], color_bg);
        memcpy(colors_fg, colors_fg + cols, (uint)cols * (rows - 1) * sizeof(uint32_t));
        std::fill(&colors_fg[cols * (rows - 1)], &colors_fg[cols * (rows)], color_fg);
    }

    void cgui::draw_char(const char& c) {
        if (input_state && c) {
            forward(ptr_rx, ptr_ry, true);
            if (!(ptr_x == ptr_rx && ptr_y == ptr_ry)) {
                for (auto i = ptr_ry * cols + ptr_rx; i > ptr_y * cols + ptr_x; --i) {
                    buffer[i] = buffer[i - 1];
                    colors_bg[i] = colors_bg[i - 1];
                    colors_fg[i] = colors_fg[i - 1];
                }
            }
        }
        buffer[ptr_y * cols + ptr_x] = c;
        colors_bg[ptr_y * cols + ptr_x] = color_bg;
        colors_fg[ptr_y * cols + ptr_x] = color_fg;
    }

    void cgui::error(const string_t & str) {
        throw cexception(ex_gui, str);
    }

    void cgui::set_cycle(int cycle) {
        if (cycle == 0) {
            cycle_set = false;
            this->cycle = GUI_CYCLES;
        }
        else {
            cycle_set = true;
            this->cycle = cycle;
        }
    }

    void cgui::set_ticks(int ticks) {
        this->ticks = ticks;
    }

    void cgui::move(bool left) {
        if (left) {
            if (ptr_mx + ptr_my * cols < ptr_x + ptr_y * cols) {
                forward(ptr_x, ptr_y, false);
            }
        }
        else {
            if (ptr_x + ptr_y * cols < ptr_rx + ptr_ry * cols) {
                forward(ptr_x, ptr_y, true);
            }
        }
    }

    void cgui::forward(int& x, int& y, bool forward) {
        if (forward) {
            if (x == cols - 1) {
                x = 0;
                if (y != rows - 1) {
                    y++;
                }
            }
            else {
                x++;
            }
        }
        else {
            if (y == 0) {
                if (x != 0) {
                    x--;
                }
            }
            else {
                if (x != 0) {
                    x--;
                }
                else {
                    x = cols - 1;
                    y--;
                }
            }
        }
    }

    string_t cgui::input_buffer() const {
        auto begin = ptr_mx + ptr_my * cols;
        auto end = ptr_x + ptr_y * cols;
        std::stringstream ss;
        for (int i = begin; i <= end; ++i) {
            if (buffer[i])
                ss << buffer[i];
        }
        return ss.str();
    }

    void cgui::resize(int r, int c) {
        if (r == 0 && c == 0) {
            r = GUI_ROWS;
            c = GUI_COLS;
        }
        auto old_rows = rows;
        auto old_cols = cols;
        rows = max(10, min(r, 60));
        cols = max(20, min(c, 200));
        ATLTRACE("[SYSTEM] GUI  | Resize: from (%d, %d) to (%d, %d)\n", old_rows, old_cols, rows, cols);
        size = rows * cols;
        auto old_buffer = buffer;
        buffer = memory.alloc_array<char>((uint)size);
        assert(buffer);
        if (!buffer)
            error("gui memory overflow");
        memset(buffer, 0, (uint)size);
        auto old_bg = colors_bg;
        colors_bg = memory.alloc_array<uint32_t>((uint)size);
        assert(colors_bg);
        if (!colors_bg)
            error("gui memory overflow");
        std::fill(colors_bg, colors_bg + size, 0);
        auto old_fg = colors_fg;
        colors_fg = memory.alloc_array<uint32_t>((uint)size);
        assert(colors_fg);
        if (!colors_fg)
            error("gui memory overflow");
        std::fill(colors_fg, colors_fg + size, MAKE_RGB(255, 255, 255));
        auto min_rows = min(old_rows, rows);
        auto min_cols = min(old_cols, cols);
        auto delta_rows = old_rows - min_rows;
        for (int i = 0; i < min_rows; ++i) {
            for (int j = 0; j < min_cols; ++j) {
                buffer[i * cols + j] = old_buffer[(delta_rows + i) * old_cols + j];
                colors_bg[i * cols + j] = old_bg[(delta_rows + i) * old_cols + j];
                colors_fg[i * cols + j] = old_fg[(delta_rows + i) * old_cols + j];
            }
        }
        ptr_x = min(ptr_x, cols);
        ptr_y = min(ptr_y, rows);
        ptr_mx = min(ptr_mx, cols);
        ptr_my = min(ptr_my, rows);
        ptr_rx = min(ptr_rx, cols);
        ptr_ry = min(ptr_ry, rows);
        memory.free(old_buffer);
        memory.free(old_fg);
        memory.free(old_bg);
    }

    void cgui::load_dep(string_t & path, std::unordered_set<string_t> & deps) {
        auto f = cache_code.find(path);
        if (f != cache_code.end()) {
            deps.insert(cache_dep[path].begin(), cache_dep[path].end());
            return;
        }
        auto code = load_file(path);
        static string_t pat_inc{ "#include[ ]+\"([/A-Za-z0-9_-]+?)\"" };
        static std::regex re_inc(pat_inc);
        std::smatch res;
        auto begin = code.cbegin();
        auto end = code.cend();
        std::vector<std::tuple<int, int, string_t>> records;
        {
            auto offset = 0;
            while (std::regex_search(begin, end, res, re_inc)) {
                if (res[1].str() == path) {
                    error("cannot include self: " + path);
                }
                if (offset + res.position() > 0) {
                    if (code[offset + res.position() - 1] != '\n') {
                        error("invalid include: " + res[1].str());
                    }
                }
                records.emplace_back(offset + res.position(),
                    offset + res.position() + res.length(),
                    res[1].str());
                offset += std::distance(begin, res[0].second);
                begin = res[0].second;
            }
        }
        if (!records.empty()) {
            // has #include directive
            std::unordered_set<string_t> _deps;
            for (auto& r : records) {
                auto& include_path = std::get<2>(r);
                load_dep(include_path, _deps);
                _deps.insert(include_path);
            }
            std::stringstream sc;
            size_t prev = 0;
            for (auto& r : records) {
                auto& start = std::get<0>(r);
                auto& length = std::get<1>(r);
                if (prev < start - prev) {
                    auto frag = code.substr((uint)prev, (uint)start - prev);
                    sc << frag;
                }
                prev = length;
            }
            if (prev < code.length()) {
                auto frag = code.substr((uint)prev, code.length() - (uint)prev);
                sc << frag;
            }
            cache_code.insert(std::make_pair(path, sc.str()));
            cache_dep.insert(std::make_pair(path, _deps));
        }
        else {
            // no #include directive
            cache_code.insert(std::make_pair(path, code));
            cache_dep.insert(std::make_pair(path, std::unordered_set<string_t>()));
        }
        load_dep(path, deps);
    }

    string_t cgui::do_include(string_t & path) { // DAG solution for include
        std::vector<string_t> v; // VERTEX(Map id to name)
        std::unordered_map<string_t, int> deps; // VERTEX(Map name to id)
        {
            std::unordered_set<string_t> _deps;
            load_dep(path, _deps);
            if (_deps.empty())
                return cache_code[path]; // no include
            _deps.insert(path);
            v.resize(_deps.size());
            std::copy(_deps.begin(), _deps.end(), v.begin());
            int i = 0;
            for (auto& d : v) {
                deps.insert(std::make_pair(d, i++));
            }
        }
        auto n = v.size();
        std::vector<std::vector<bool>> DAG(n); // DAG(Map id to id)
        std::unordered_set<size_t> deleted;
        std::vector<size_t> topo; // 拓扑排序
        for (size_t i = 0; i < n; ++i) {
            DAG[i].resize(n);
            for (size_t j = 0; j < n; ++j) {
                auto& _d = cache_dep[v[i]];
                if (_d.find(v[j]) != _d.end())
                    DAG[i][j] = true;
            }
        }
        // DAG[i][j] == true  =>  i 包含 j
        for (size_t i = 0; i < n; ++i) { // 每次找出零入度点并删除
            size_t right = n;
            for (size_t j = 0; j < n; ++j) { // 找出零入度点
                if (deleted.find(j) == deleted.end()) {
                    bool success = true;
                    for (size_t k = 0; k < n; ++k) {
                        if (DAG[j][k]) {
                            success = false;
                            break;
                        }
                    }
                    if (success) { // 找到
                        right = j;
                        break;
                    }
                }
            }
            if (right != n) {
                for (size_t k = 0; k < n; ++k) { // 删除点
                    DAG[k][right] = false;
                }
                topo.push_back(right);
                deleted.insert(right);
            }
        }
        if (topo.size() != n) {
            error("topo failed: " + path);
        }
#if LOG_DEP
        ATLTRACE("[SYSTEM] DEP  | ---------------\n");
        ATLTRACE("[SYSTEM] DEP  | PATH: %s\n", path.c_str());
        for (size_t i = 0; i < n; ++i) {
            ATLTRACE("[SYSTEM] DEP  | [%d] ==> %s\n", i, v[topo[i]].c_str());
        }
        ATLTRACE("[SYSTEM] DEP  | ---------------\n");
#endif
        std::stringstream ss;
        for (auto& tp : topo) {
            ss << cache_code[v[tp]];
        }
        return ss.str();
    }

    int cgui::compile(const string_t & path, const std::vector<string_t> & args) {
        if (path.empty())
            return -1;
        auto fail_errno = -1;
        auto new_path = path;
        try {
            auto c = cache.find(new_path);
            if (c != cache.end()) {
                return vm->load(new_path, c->second, args);
            }
            auto code = do_include(new_path);
            fail_errno = -2;
            gen.reset();
            auto root = p.parse(code, &gen);
#if LOG_AST
            cast::print(root, 0, std::cout);
#endif
            gen.gen(root);
            auto file = gen.file();
            p.clear_ast();
            cache.insert(std::make_pair(new_path, file));
            return vm->load(new_path, file, args);
        }
        catch (const cexception & e) {
            gen.reset();
            ATLTRACE("[SYSTEM] ERR  | PATH: %s, %s\n", new_path.c_str(), e.message().c_str());
            return fail_errno;
        }
    }

    void cgui::input_set(bool valid) {
        if (valid) {
            input_state = true;
            ptr_mx = ptr_x;
            ptr_my = ptr_y;
            ptr_rx = ptr_x;
            ptr_ry = ptr_y;
        }
        else {
            input_state = false;
            ptr_mx = -1;
            ptr_my = -1;
            ptr_rx = -1;
            ptr_ry = -1;
        }
        input_ticks = 0;
        input_caret = false;
    }

    void cgui::input(int c) {
        if (c == 3) {
            cvm::global_state.interrupt = true;
            if (input_state) {
                ptr_x = ptr_rx;
                ptr_y = ptr_ry;
                put_char('\n');
                cvm::global_state.input_content.clear();
                cvm::global_state.input_read_ptr = 0;
                cvm::global_state.input_success = true;
                input_state = false;
            }
            return;
        }
        if (!input_state)
            return;
        if (!((c & GUI_SPECIAL_MASK) || std::isprint(c) || c == '\b' || c == '\n' || c == '\r' || c == 4 || c == 7 || c == 26)) {
            ATLTRACE("[SYSTEM] GUI  | Input: %d\n", (int)c);
            return;
        }
        if (c == '\b') {
            put_char('\b');
            return;
        }
        if (c == '\r' || c == 4 || c == 26) {
            ptr_x = ptr_rx;
            ptr_y = ptr_ry;
            put_char('\n');
            cvm::global_state.input_content = input_buffer();
            cvm::global_state.input_read_ptr = 0;
            cvm::global_state.input_success = true;
            input_state = false;
            return;
        }
        if (c & GUI_SPECIAL_MASK) {
            char C = (char)-9;
            switch (c & 0xff) {
            case VK_LEFT:
                // C = (char) -12;
                // break;
                move(true);
                return;
            case VK_UP:
                C = (char)-10;
                break;
            case VK_RIGHT:
                // C = (char) -13;
                // break;
                move(false);
                return;
            case VK_DOWN:
                C = (char)-11;
                break;
            case VK_HOME:
                ptr_x = ptr_mx;
                ptr_y = ptr_my;
                return;
            case VK_END:
                ptr_x = ptr_rx;
                ptr_y = ptr_ry;
                return;
            case 0x71: // SHIFT
                return;
            case 0x72: // CTRL
                return;
            case 0x74: // ALT
                return;
            default:
                printf("invalid special key: %d\n", c & 0xff);
                return;
            }
            cvm::global_state.input_content = input_buffer();
            cvm::global_state.input_content.push_back(C);
            cvm::global_state.input_read_ptr = 0;
            cvm::global_state.input_success = true;
            input_state = false;
            auto begin = ptr_mx + ptr_my * cols;
            auto end = ptr_x + ptr_y * cols;
            for (int i = begin; i <= end; ++i) {
                buffer[i] = 0;
                colors_bg[i] = color_bg;
                colors_fg[i] = color_fg;
            }
            ptr_x = ptr_mx;
            ptr_y = ptr_my;
            ptr_rx = ptr_mx;
            ptr_ry = ptr_my;
        }
        else {
            put_char((char)(c & 0xff));
        }
    }

    void cgui::reset_cmd() {
        cmd_state = false;
        cmd_string.clear();
    }

    int cgui::reset_cycles() {
        auto c = cycles;
        cycles = 0;
        return c;
    }

    void cgui::exec_cmd(const string_t & s) {
        switch (s[0]) {
        case 'B': { // 设置背景色
            color_bg = (uint32_t)std::stoul(s.substr(1), nullptr, 16);
        }
                  break;
        case 'F': { // 设置前景色
            color_fg = (uint32_t)std::stoul(s.substr(1), nullptr, 16);
        }
                  break;
        case 'S': { // 设置
            static int cfg;
            cfg = (uint32_t)std::stoul(s.substr(1), nullptr, 10);
            switch (cfg) {
            case 0: { // 换行
                if (ptr_x > 0) {
                    ptr_x = 0;
                    if (ptr_y == rows - 1) {
                        new_line();
                    }
                    else {
                        ptr_y++;
                    }
                }
            }
                    break;
            case 1: // 保存背景色
                color_bg_stack.push_back(color_bg);
                break;
            case 2: // 保存前景色
                color_fg_stack.push_back(color_fg);
                break;
            case 3: // 恢复背景色
                color_bg = color_bg_stack.back();
                if (color_bg_stack.size() > 1) color_bg_stack.pop_back();
                break;
            case 4: // 恢复前景色
                color_fg = color_fg_stack.back();
                if (color_fg_stack.size() > 1) color_fg_stack.pop_back();
                break;
            default:
                break;
            }
        }
                  break;
        default:
            break;
        }
    }
}
