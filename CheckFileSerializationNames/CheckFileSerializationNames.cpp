#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

struct FileInfo
{
    std::string fileName;
    std::string numberText; // 正規化済み数値文字列
    std::size_t digitWidth; // 元の桁数（0埋め表示用）
};

struct Range
{
    std::string start;
    std::string end;
};

std::string ExtractFirstDigitSequence(const std::string& text)
{
    const auto begin = std::find_if(text.begin(), text.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });

    if (begin == text.end())
    {
        return {};
    }

    const auto end = std::find_if_not(begin, text.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });

    return std::string(begin, end);
}   

std::string_view TrimLeadingZeros(std::string_view text)
{
    const auto firstNonZero = text.find_first_not_of('0');
    if (firstNonZero == std::string_view::npos)
    {
        return text.substr(text.size() - 1);
    }

    return text.substr(firstNonZero);
}

std::string NormalizeNumber(std::string_view text)
{
    return std::string(TrimLeadingZeros(text));
}

int CompareNumericStrings(std::string_view left, std::string_view right)
{
    left = TrimLeadingZeros(left);
    right = TrimLeadingZeros(right);

    if (left.size() != right.size())
    {
        return left.size() < right.size() ? -1 : 1;
    }

    if (left == right)
    {
        return 0;
    }

    return left < right ? -1 : 1;
}

std::string IncrementNumericString(const std::string& text)
{
    std::string result = text;

    int carry = 1;
    for (auto it = result.rbegin(); it != result.rend() && carry != 0; ++it)
    {
        const int digit = (*it - '0') + carry;
        *it = static_cast<char>('0' + (digit % 10));
        carry = digit / 10;
    }

    if (carry != 0)
    {
        result.insert(result.begin(), '1');
    }

    return result;
}

std::string DecrementNumericString(const std::string& text)
{
    if (text == "0")
    {
        return "0";
    }

    std::string result = text;

    int borrow = 1;
    for (auto it = result.rbegin(); it != result.rend() && borrow != 0; ++it)
    {
        int digit = (*it - '0') - borrow;
        if (digit < 0)
        {
            *it = '9';
            borrow = 1;
        }
        else
        {
            *it = static_cast<char>('0' + digit);
            borrow = 0;
        }
    }

    return NormalizeNumber(result);
}

std::string PadLeft(std::string_view text, std::size_t width)
{
    if (text.size() >= width)
    {
        return std::string(text);
    }

    return std::string(width - text.size(), '0') + std::string(text);
}

int main()
{
    const fs::path currentDirectory = fs::current_path();
    std::vector<FileInfo> fileInfos;

    for (const auto& entry : fs::directory_iterator(currentDirectory))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        const auto fileName = entry.path().filename().string();
        const auto baseName = entry.path().stem().string();
        const auto numberText = ExtractFirstDigitSequence(baseName);

        if (numberText.empty())
        {
            continue;
        }

        fileInfos.push_back(FileInfo{
            fileName,
            NormalizeNumber(numberText),
            numberText.size()
        });
    }

    std::sort(fileInfos.begin(), fileInfos.end(), [](const FileInfo& left, const FileInfo& right) {
        const int compare = CompareNumericStrings(left.numberText, right.numberText);
        if (compare != 0)
        {
            return compare < 0;
        }

        return left.fileName < right.fileName;
    });

    if (fileInfos.size() < 2)
    {
        std::cout << "連番を判定できるファイルが2件以上見つかりませんでした。\n";
        return 0;
    }

    std::size_t padWidth = 0;
    for (const auto& info : fileInfos)
    {
        padWidth = std::max(padWidth, info.digitWidth);
    }

    std::vector<Range> missingRanges;
    for (std::size_t i = 1; i < fileInfos.size(); ++i)
    {
        const auto& previous = fileInfos[i - 1].numberText;
        const auto& current = fileInfos[i].numberText;

        const auto nextExpected = IncrementNumericString(previous);
        if (CompareNumericStrings(current, nextExpected) <= 0)
        {
            continue;
        }

        missingRanges.push_back(Range{
            nextExpected,
            DecrementNumericString(current)
        });
    }

    std::cout << "対象フォルダ: " << currentDirectory.string() << '\n';
    std::cout << "連番付きファイル数: " << fileInfos.size() << '\n';

    if (missingRanges.empty())
    {
        std::cout << "番号の抜けは見つかりませんでした。\n";
        return 0;
    }

    std::cout << "抜けている番号:\n";
    for (const auto& range : missingRanges)
    {
        const auto start = PadLeft(range.start, padWidth);
        const auto end = PadLeft(range.end, padWidth);

        if (range.start == range.end)
        {
            std::cout << "- " << start << '\n';
        }
        else
        {
            std::cout << "- " << start << " ～ " << end << '\n';
        }
    }

    return 0;
}
