#include "TestFramework.hpp"
#include "../include/MetadataUtils.hpp"

TEST_CASE("Lyrics Romanization", "KanaToRomaji converts hiragana and katakana with sokuon") {
    std::string kana1 = "とうほうぷろじぇくと";
    std::string rom1 = KanaToRomaji(kana1);
    ASSERT_STR_EQ(rom1, "touhoupurojekuto");

    std::string kana2 = "きょうふさえあまく";
    std::string rom2 = KanaToRomaji(kana2);
    ASSERT_STR_EQ(rom2, "kyoufusaeamaku");

    std::string kanaSokuon = "きっちり";
    std::string romSokuon = KanaToRomaji(kanaSokuon);
    ASSERT_STR_EQ(romSokuon, "kitchiri");
}

TEST_CASE("Lyrics Romanization", "RomanizeJapaneseLyrics handles furigana in parentheses and keeps timestamps") {
    std::string text = "[00:22.50]私の運命(さだめ)\n[00:30.00]Hello world";
    std::string res = RomanizeJapaneseLyrics(text);
    
    // Check that furigana "さだめ" became "sadame" and timestamp "[00:22.50]" is preserved
    ASSERT_TRUE(res.find("[00:22.50]") != std::string::npos);
    ASSERT_TRUE(res.find("sadame") != std::string::npos);
    ASSERT_TRUE(res.find("[00:30.00]Hello world") != std::string::npos);
}
