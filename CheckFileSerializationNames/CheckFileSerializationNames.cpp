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
// Using path::u8string() (C++20) to obtain UTF-8 bytes directly from std::filesystem::path.
// MSVC with C++20 returns a UTF-8 encoded std::string from path::u8string().

namespace fs = std::filesystem;

struct FileInfo
{
    std::u8string fileNameUtf8;   // UTF-8 に変換したファイル名（表示・ソート用）
    std::u8string numberText;     // 正規化済み（先頭ゼロ除去）数値文字列（比較用）
    std::size_t digitWidth;       // 元の数字列の桁数（ゼロ埋め表示用）
};

struct Range
{
    std::u8string start;
    std::u8string end;
};

static std::u8string ToU8(const std::string& s)
{
    return std::u8string(reinterpret_cast<const char8_t*>(s.data()), reinterpret_cast<const char8_t*>(s.data()) + s.size());
}

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

// ワイド文字列から最初の連続する数字列を抽出し、ASCII の狭義文字列として返す。
// 例: L"img_😀0123a" -> "0123"
// kept for compatibility but not used when path::u8string() is available
static std::string ExtractFirstDigitSequenceFromWide(const std::wstring& text)
{
    auto it = std::find_if(text.begin(), text.end(), [](wchar_t ch) { return std::iswdigit(ch); });
    if (it == text.end()) return {};
    auto it2 = std::find_if_not(it, text.end(), [](wchar_t ch) { return std::iswdigit(ch); });
    std::string out;
    out.reserve(static_cast<size_t>(it2 - it));
    for (auto i = it; i != it2; ++i)
    {
        out.push_back(static_cast<char>(*i));
    }
    return out;
}

static std::basic_string_view<char8_t> TrimLeadingZeros(std::basic_string_view<char8_t> text)
{
    const auto pos = text.find_first_not_of(static_cast<char8_t>('0'));
    if (pos == std::basic_string_view<char8_t>::npos)
    {
        return text.substr(text.size() - 1);
    }
    return text.substr(pos);
}

// 入力は数字のみの文字列を想定。比較用は正規化済み文字列（先頭ゼロ除去）を用いるのでここは軽量に。
static int CompareNumericStringsNormalized(std::basic_string_view<char8_t> a, std::basic_string_view<char8_t> b)
{
    a = TrimLeadingZeros(a);
    b = TrimLeadingZeros(b);
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    if (a == b) return 0;
    // lexicographical compare on byte sequences is fine for equal-length ASCII digit sequences
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
    // if normalized zero
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

int main()
{
    const fs::path cwd = fs::current_path();
    std::vector<FileInfo> fileInfos;
    fileInfos.reserve(256);

    // 列挙はファイルシステムメタデータの読み取りのみ。ファイル内容の読み書きは行わない。
    for (const auto& entry : fs::directory_iterator(cwd))
    {
        if (!entry.is_regular_file()) continue;
        const std::u8string fname_bytes = entry.path().filename().u8string();
        const std::u8string stem_bytes = entry.path().stem().u8string();

        // extract digits from UTF-8 bytes (ASCII digits)
        const std::u8string digits = ExtractFirstDigitSequenceFromUtf8(std::basic_string_view<char8_t>(stem_bytes));
        if (digits.empty()) continue;

        const std::u8string normalized = NormalizeNumber(std::basic_string_view<char8_t>(digits));
        const std::u8string fname_utf8 = fname_bytes;

        fileInfos.push_back(FileInfo{ fname_utf8, normalized, static_cast<std::size_t>(digits.size()) });
    }

    if (fileInfos.size() < 2)
    {
        std::cout << "連番を判定できるファイルが2件以上見つかりませんでした。\n";
        return 0;
    }

    // 数字列は既に正規化済みなので比較は高速
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
    const std::string_view cwdSv(reinterpret_cast<const char*>(cwdUtf8.data()), cwdUtf8.size());
    std::cout << "対象フォルダ: " << cwdSv << '\n';
    std::cout << "連番付きファイル数: " << fileInfos.size() << '\n';

    if (missing.empty())
    {
        std::cout << "番号の抜けは見つかりませんでした。\n";
        return 0;
    }

    std::cout << "抜けている番号:\n";
    for (const auto& r : missing)
    {
        const auto s_u8 = PadLeft(std::basic_string_view<char8_t>(r.start), padWidth);
        const auto e_u8 = PadLeft(std::basic_string_view<char8_t>(r.end), padWidth);
        const std::string_view s_sv(reinterpret_cast<const char*>(s_u8.data()), s_u8.size());
        const std::string_view e_sv(reinterpret_cast<const char*>(e_u8.data()), e_u8.size());
        if (r.start == r.end) std::cout << "- " << s_sv << '\n';
        else std::cout << "- " << s_sv << " ～ " << e_sv << '\n';
    }

    return 0;
}
