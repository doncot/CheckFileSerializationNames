#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <string_view>

#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <Windows.h>

namespace fs = std::filesystem;

struct FileInfo
{
    std::u8string fileNameUtf8;   // UTF-8 のファイル名
    std::u8string numberText;     // 正規化済み（先頭ゼロ除去）数値文字列（UTF-8 bytes）
    std::size_t digitWidth;
};

struct Range
{
    std::u8string start;
    std::u8string end;
};

static std::u8string ExtractFirstDigitSequenceFromUtf8(std::basic_string_view<char8_t> sv)
{
    const auto it = std::find_if(sv.begin(), sv.end(), [](char8_t ch) { return std::isdigit(static_cast<unsigned char>(static_cast<char>(ch))); });
    if (it == sv.end()) return {};
    const auto it2 = std::find_if_not(it, sv.end(), [](char8_t ch) { return std::isdigit(static_cast<unsigned char>(static_cast<char>(ch))); });
    std::u8string out;
    out.reserve(static_cast<size_t>(it2 - it));
    for (auto p = it; p != it2; ++p) out.push_back(*p);
    return out;
}

static std::basic_string_view<char8_t> TrimLeadingZeros(std::basic_string_view<char8_t> text)
{
    const auto pos = text.find_first_not_of(static_cast<char8_t>('0'));
    if (pos == std::basic_string_view<char8_t>::npos) return text.substr(text.size() - 1);
    return text.substr(pos);
}

static int CompareNumericStringsNormalized(std::basic_string_view<char8_t> a, std::basic_string_view<char8_t> b)
{
    a = TrimLeadingZeros(a);
    b = TrimLeadingZeros(b);
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    if (a == b) return 0;
    return a < b ? -1 : 1;
}

static std::u8string NormalizeNumber(std::basic_string_view<char8_t> text)
{
    const auto v = TrimLeadingZeros(text);
    return std::u8string(v.begin(), v.end());
}

static std::u8string IncrementNumericString(const std::u8string& text)
{
    std::u8string result = text;
    int carry = 1;
    for (auto it = result.rbegin(); it != result.rend() && carry; ++it)
    {
        int d = (static_cast<char>(*it) - '0') + carry;
        char out = static_cast<char>('0' + (d % 10));
        *it = static_cast<char8_t>(out);
        carry = d / 10;
    }
    if (carry) result.insert(result.begin(), static_cast<char8_t>('1'));
    return result;
}

static std::u8string DecrementNumericString(const std::u8string& text)
{
    std::u8string result = text;
    if (result.size() == 1 && result[0] == static_cast<char8_t>('0')) return std::u8string(u8"0");
    int borrow = 1;
    for (auto it = result.rbegin(); it != result.rend() && borrow; ++it)
    {
        int d = (static_cast<char>(*it) - '0') - borrow;
        if (d < 0) { *it = static_cast<char8_t>('9'); borrow = 1; }
        else { *it = static_cast<char8_t>(static_cast<char>('0' + d)); borrow = 0; }
    }
    return NormalizeNumber(result);
}

static std::u8string PadLeft(std::basic_string_view<char8_t> text, std::size_t width)
{
    if (text.size() >= width) return std::u8string(text.begin(), text.end());
    std::u8string out;
    out.reserve(width);
    out.append(width - text.size(), static_cast<char8_t>('0'));
    out.append(text.begin(), text.end());
    return out;
}

static std::wstring WideFromUtf8(std::u8string_view s)
{
    if (s.empty()) return {};
    const char* bytes = reinterpret_cast<const char*>(s.data());
    int len = static_cast<int>(s.size());
    int wlen = ::MultiByteToWideChar(CP_UTF8, 0, bytes, len, nullptr, 0);
    if (wlen == 0) return {};
    std::wstring ws;
    ws.resize(wlen);
    ::MultiByteToWideChar(CP_UTF8, 0, bytes, len, &ws[0], wlen);
    return ws;
}

static void WriteWideLine(const std::wstring& ws)
{
    HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        DWORD written = 0;
        if (::GetConsoleMode(h, &mode))
        {
            ::WriteConsoleW(h, ws.c_str(), static_cast<DWORD>(ws.size()), &written, nullptr);
            ::WriteConsoleW(h, L"\n", 1, &written, nullptr);
            return;
        }
    }
    int mbsLen = ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    if (mbsLen > 0)
    {
        std::string out;
        out.resize(mbsLen);
        ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), out.data(), mbsLen, nullptr, nullptr);
        std::cout << out << '\n';
    }
}

static void PrintLabelAndUtf8Value(const std::wstring& labelWide, std::u8string_view value)
{
    const std::wstring valWide = WideFromUtf8(value);
    WriteWideLine(labelWide + valWide);
}

int main()
{
    const fs::path cwd = fs::current_path();
    std::vector<FileInfo> fileInfos;
    fileInfos.reserve(256);

    for (const auto& entry : fs::directory_iterator(cwd))
    {
        if (!entry.is_regular_file()) continue;
        const std::u8string fname_bytes = entry.path().filename().u8string();
        const std::u8string stem_bytes = entry.path().stem().u8string();

        const std::u8string digits = ExtractFirstDigitSequenceFromUtf8(std::basic_string_view<char8_t>(stem_bytes));
        if (digits.empty()) continue;

        const std::u8string normalized = NormalizeNumber(std::basic_string_view<char8_t>(digits));
        const std::u8string fname_utf8 = fname_bytes;

        fileInfos.push_back(FileInfo{ fname_utf8, normalized, static_cast<std::size_t>(digits.size()) });
    }

    if (fileInfos.size() < 2)
    {
        WriteWideLine(std::wstring(L"連番を判定できるファイルが2件以上見つかりませんでした。"));
        return 0;
    }

    std::sort(fileInfos.begin(), fileInfos.end(), [](const FileInfo& a, const FileInfo& b) {
        const int cmp = CompareNumericStringsNormalized(std::basic_string_view<char8_t>(a.numberText), std::basic_string_view<char8_t>(b.numberText));
        if (cmp != 0) return cmp < 0;
        const std::string_view an(reinterpret_cast<const char*>(a.fileNameUtf8.data()), a.fileNameUtf8.size());
        const std::string_view bn(reinterpret_cast<const char*>(b.fileNameUtf8.data()), b.fileNameUtf8.size());
        return an < bn;
    });

    std::size_t padWidth = 0;
    for (const auto& f : fileInfos) padWidth = std::max(padWidth, f.digitWidth);

    std::vector<Range> missing;
    missing.reserve(64);

    for (std::size_t i = 1; i < fileInfos.size(); ++i)
    {
        const auto& prev = fileInfos[i - 1].numberText;
        const auto& cur  = fileInfos[i].numberText;

        const std::u8string nextExpected = IncrementNumericString(prev);
        if (CompareNumericStringsNormalized(std::basic_string_view<char8_t>(cur), std::basic_string_view<char8_t>(nextExpected)) <= 0) continue;

        missing.push_back(Range{ nextExpected, DecrementNumericString(cur) });
    }

    const std::u8string cwdUtf8 = cwd.u8string();
    PrintLabelAndUtf8Value(L"対象フォルダ: ", std::basic_string_view<char8_t>(cwdUtf8));
    WriteWideLine(std::wstring(L"連番付きファイル数: ") + std::to_wstring(fileInfos.size()));

    if (missing.empty())
    {
        WriteWideLine(std::wstring(L"番号の抜けは見つかりませんでした。"));
        return 0;
    }

    WriteWideLine(std::wstring(L"抜けている番号:"));
    for (const auto& r : missing)
    {
        const std::u8string s_u8 = PadLeft(std::basic_string_view<char8_t>(r.start), padWidth);
        const std::u8string e_u8 = PadLeft(std::basic_string_view<char8_t>(r.end), padWidth);
        const std::wstring s_w = WideFromUtf8(std::basic_string_view<char8_t>(s_u8));
        const std::wstring e_w = WideFromUtf8(std::basic_string_view<char8_t>(e_u8));
        if (r.start == r.end)
        {
            WriteWideLine(std::wstring(L"- ") + s_w);
        }
        else
        {
            WriteWideLine(std::wstring(L"- ") + s_w + L" ～ " + e_w);
        }
    }

    return 0;
}
