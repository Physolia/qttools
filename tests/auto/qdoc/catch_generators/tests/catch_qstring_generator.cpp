/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the tools applications of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "namespaces.hpp"
#include "generators/qchar_generator.hpp"
#include "generators/qstring_generator.hpp"

#include <qt_catch_conversions.hpp>

#include <catch.hpp>

using namespace QDOC_CATCH_GENERATORS_ROOT_NAMESPACE;

#include <algorithm>

SCENARIO("Binding a QString to a length range", "[QString][Bounds]") {
    GIVEN("A minimum length") {
        auto minimum_length = GENERATE(take(100, random(0, 100)));

        AND_GIVEN("A maximum length that is greater or equal than the minimum length") {
            auto maximum_length = GENERATE_COPY(take(100, random(minimum_length, 100)));

            WHEN("A QString is generated from those bounds") {
                QString generated_string = GENERATE_COPY(take(1, string(character(), minimum_length, maximum_length)));

                THEN("The generated string's length is in the range [minimum_length, maximum_length]") {
                    REQUIRE(generated_string.length() >= minimum_length);
                    REQUIRE(generated_string.length() <= maximum_length);
                }
            }
        }
    }
}

TEST_CASE("When the maximum length and the minimum length are zero all generated strings are the empty string", "[QString][Bounds][SpecialCase][BoundingValue]") {
    QString generated_string = GENERATE(take(100, string(character(), 0, 0)));

    REQUIRE(generated_string.isEmpty());
}

TEST_CASE("When the maximum length and the minimum length are equal, all generated strings have the same length equal to the given length", "[QString][Bounds][SpecialCase]") {
    auto length = GENERATE(take(100, random(0, 100)));
    auto generated_string = GENERATE_COPY(take(100, string(character(), length, length)));

    REQUIRE(generated_string.length() == length);
}

SCENARIO("Limiting the characters that can compose a QString", "[QString][Contents]") {
    GIVEN("A list of characters candidates") {
        auto lower_character_bound = GENERATE(take(10, random(
            static_cast<unsigned int>(std::numeric_limits<char16_t>::min()),
            static_cast<unsigned int>(std::numeric_limits<char16_t>::max())
        )));
        auto upper_character_bound = GENERATE_COPY(take(10, random(lower_character_bound, static_cast<unsigned int>(std::numeric_limits<char16_t>::max()))));

        auto character_candidates = character(lower_character_bound, upper_character_bound);

        WHEN("A QString is generated from that list") {
            QString generated_string = GENERATE_REF(take(100, string(std::move(character_candidates), 1, 50)));

            THEN("The string is composed only of characters that are in the list of characters") {
                REQUIRE(
                    std::all_of(
                        generated_string.cbegin(), generated_string.cend(),
                        [lower_character_bound, upper_character_bound](QChar element){ return element.unicode() >= lower_character_bound && element.unicode() <= upper_character_bound; }
                    )
                );
            }
        }
    }
}

TEST_CASE("The strings generated by a generator of empty string are all empty", "[QString][Contents]") {
    QString generated_string = GENERATE(take(100, empty_string()));

    REQUIRE(generated_string.isEmpty());
}


TEST_CASE("The first element of the passsed in generator is not lost", "[QString][GeneratorFirstElement][SpecialCase]") {
    QChar first_value{'a'};

    // REMARK: We use two values to avoid having the generator throw
    // an exception if the first element is actually lost.
    auto character_generator{Catch::Generators::values({first_value, QChar{'b'}})};
    auto generated_string = GENERATE_REF(take(1, string(std::move(character_generator), 1, 1)));

    REQUIRE(generated_string == QString{first_value});
}
