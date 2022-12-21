// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "validator.h"

#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "attribute.h"
#include "errors.h"
#include "frame.h"

namespace ipp {
namespace {

constexpr std::string_view kAllowedCharsInKeyword = "-_.";
constexpr std::string_view kAllowedCharsInUri = ":/?#[]@!$&'()*+,;=-._~%";
constexpr std::string_view kAllowedCharsInUriScheme = "+-.";
constexpr std::string_view kAllowedCharsInNaturalLanguage = "-";

bool IsLowercaseLetter(char c) {
  return c >= 'a' && c <= 'z';
}

bool IsUppercaseLetter(char c) {
  return c >= 'A' && c <= 'Z';
}

bool IsDigit(char c) {
  return c >= '1' && c <= '9';
}

// Helper struct for string validation.
struct StringValidator {
  // The input string to validate.
  std::string_view value;
  // Output set of error codes.
  std::set<ValidationCode> codes;
  // Validates the string length.
  void CheckLength(size_t max_length, bool empty_string_allowed = false) {
    if (value.empty()) {
      if (!empty_string_allowed) {
        codes.insert(ValidationCode::kStringEmpty);
      }
      return;
    }
    if (value.size() > max_length) {
      codes.insert(ValidationCode::kStringTooLong);
    }
  }
  // Checks if the string starts from lowercase letter. It does nothing if
  // the input string is empty.
  void CheckFirstLetterIsLowercase() {
    if (value.empty() || IsLowercaseLetter(value.front()))
      return;
    codes.insert(ValidationCode::kStringMustStartLowercaseLetter);
  }
  // Checks if the input string consists only of letters, digits and
  // characters from `allowed_chars`.
  void CheckLettersDigits(std::string_view allowed_chars,
                          bool uppercase_letters_allowed = false) {
    for (char c : value) {
      if (IsLowercaseLetter(c))
        continue;
      if (uppercase_letters_allowed && IsUppercaseLetter(c))
        continue;
      if (IsDigit(c))
        continue;
      if (allowed_chars.find(c) == std::string_view::npos) {
        codes.insert(ValidationCode::kStringInvalidCharacter);
        break;
      }
    }
  }
  // Checks if the input string consists only of printable characters.
  void CheckPrintable(bool uppercase_letters_allowed = false) {
    for (char c : value) {
      if (c < 0x20 || c > 0x7e) {
        codes.insert(ValidationCode::kStringInvalidCharacter);
        break;
      }
      if (!uppercase_letters_allowed && IsUppercaseLetter(c)) {
        codes.insert(ValidationCode::kStringInvalidCharacter);
        break;
      }
    }
  }
};

// `year` must be > 0.
bool IsLeapYear(uint16_t year) {
  if (year % 4)
    return false;
  // Is divisible by 4.
  if (year % 100)
    return true;
  // Is divisible by 4 and 100.
  return (year % 400 == 0);
}

// Validate 'text' value based on:
// * rfc8011, section 5.1.2.
std::set<ValidationCode> validateTextWithoutLanguage(std::string_view value) {
  StringValidator validator = {value};
  validator.CheckLength(kMaxLengthOfText, /*empty_string_allowed=*/true);
  return validator.codes;
}

// Validate 'name' value based on:
// * rfc8011, section 5.1.3.
std::set<ValidationCode> validateNameWithoutLanguage(std::string_view value) {
  StringValidator validator = {value};
  validator.CheckLength(kMaxLengthOfName, /*empty_string_allowed=*/true);
  return validator.codes;
}

// Validate 'keyword' value based on:
// * rfc8011, section 5.1.4.
// * rfc8011 errata
std::set<ValidationCode> validateKeyword(std::string_view value) {
  StringValidator validator = {value};
  validator.CheckLength(kMaxLengthOfKeyword);
  validator.CheckLettersDigits(kAllowedCharsInKeyword,
                               /*uppercase_letters_allowed=*/true);
  return validator.codes;
}

// Validate 'uri' value based on:
// * rfc8011, section 5.1.6;
// * rfc3986, section 2.
std::set<ValidationCode> validateUri(std::string_view value) {
  StringValidator validator = {value};
  validator.CheckLength(kMaxLengthOfUri);
  validator.CheckLettersDigits(kAllowedCharsInUri,
                               /*uppercase_letters_allowed=*/true);
  return validator.codes;
}

// Validate 'uriScheme' value based on:
// * rfc8011, section 5.1.7;
// * rfc3986, section 3.1.
std::set<ValidationCode> validateUriScheme(std::string_view value) {
  StringValidator validator = {value};
  validator.CheckLength(kMaxLengthOfUriScheme);
  validator.CheckFirstLetterIsLowercase();
  validator.CheckLettersDigits(kAllowedCharsInUriScheme);
  return validator.codes;
}

// Validate 'charset' value based on:
// * rfc8011, section 5.1.8;
// * https://www.iana.org/assignments/character-sets/character-sets.xhtml.
std::set<ValidationCode> validateCharset(std::string_view value) {
  StringValidator validator = {value};
  validator.CheckLength(kMaxLengthOfCharset);
  validator.CheckPrintable();
  return validator.codes;
}

// Validate 'naturalLanguage' value based on:
// * rfc8011, section 5.1.9;
// * rfc5646, section 2.1.
std::set<ValidationCode> validateNaturalLanguage(std::string_view value) {
  StringValidator validator = {value};
  validator.CheckLength(kMaxLengthOfNaturalLanguage);
  validator.CheckLettersDigits(kAllowedCharsInNaturalLanguage);
  return validator.codes;
}

// Validate 'mimeMediaType' value based on:
// * rfc8011, section 5.1.10;
// * https://www.iana.org/assignments/media-types/media-types.xhtml.
std::set<ValidationCode> validateMimeMediaType(std::string_view value) {
  StringValidator validator = {value};
  validator.CheckLength(kMaxLengthOfMimeMediaType);
  validator.CheckPrintable(/*uppercase_letters_allowed=*/true);
  return validator.codes;
}

// Validate 'octetString' value based on:
// * rfc8011, section 5.1.11.
std::set<ValidationCode> validateOctetString(std::string_view value) {
  std::set<ValidationCode> codes;
  if (value.size() > kMaxLengthOfOctetString)
    codes.insert(ValidationCode::kStringTooLong);
  return codes;
}

// Validate 'dateTime' value based on:
// * rfc8011, section 5.1.15;
// * DateAndTime defined in rfc2579, section 2;
// * also enforces 1970 <= year <= 2100.
std::set<ValidationCode> validateDateTime(const DateTime& value) {
  std::set<ValidationCode> codes;

  // Verify the date.
  if (value.year < 1970 || value.year > 2100 || value.month < 1 ||
      value.month > 12 || value.day < 1) {
    codes.insert(ValidationCode::kDateTimeInvalidDate);
  } else {
    uint8_t max_day = 31;
    switch (value.month) {
      case 2:
        if (IsLeapYear(value.year)) {
          max_day = 29;
        } else {
          max_day = 28;
        }
        break;
      case 4:  // FALLTHROUGH
      case 6:  // FALLTHROUGH
      case 9:  // FALLTHROUGH
      case 11:
        max_day = 30;
        break;
    }
    if (value.day > max_day) {
      codes.insert(ValidationCode::kDateTimeInvalidDate);
    }
  }

  // Verify the time of day (seconds == 60 means leap second).
  if (value.hour > 23 || value.minutes > 59 || value.seconds > 60 ||
      value.deci_seconds > 9) {
    codes.insert(ValidationCode::kDateTimeInvalidTimeOfDay);
  }

  // Verify the timezone (daylight saving time in New Zealand is +13).
  if ((value.UTC_direction != '-' && value.UTC_direction != '+') ||
      value.UTC_hours > 13 || value.UTC_minutes > 59) {
    codes.insert(ValidationCode::kDateTimeInvalidZone);
  }

  return codes;
}

// Validate 'resolution' value based on:
// * rfc8011, section 5.1.16.
std::set<ValidationCode> validateResolution(Resolution value) {
  std::set<ValidationCode> codes;
  if (value.units != Resolution::Units::kDotsPerCentimeter &&
      value.units != Resolution::Units::kDotsPerInch)
    codes.insert(ValidationCode::kResolutionInvalidUnit);
  if (value.xres < 1 || value.yres < 1)
    codes.insert(ValidationCode::kResolutionInvalidDimension);
  return codes;
}

// Validate 'rangeOfInteger' value based on:
// * rfc8011, section 5.1.14.
std::set<ValidationCode> validateRangeOfInteger(RangeOfInteger value) {
  std::set<ValidationCode> codes;
  if (value.min_value > value.max_value)
    codes.insert(ValidationCode::kRangeOfIntegerMaxLessMin);
  return codes;
}

// Validate 'textWithLanguage' value based on:
// * rfc8011, section 5.1.2.2.
std::set<ValidationCode> validateTextWithLanguage(
    const StringWithLanguage& value) {
  std::set<ValidationCode> codes = validateTextWithoutLanguage(value.value);
  if (!value.language.empty()) {
    if (!validateNaturalLanguage(value.language).empty())
      codes.insert(ValidationCode::kStringWithLangInvalidLanguage);
  }
  return codes;
}

// Validate 'nameWithLanguage' value based on:
// * rfc8011, section 5.1.3.2.
std::set<ValidationCode> validateNameWithLanguage(
    const StringWithLanguage& value) {
  std::set<ValidationCode> codes = validateNameWithoutLanguage(value.value);
  if (!value.language.empty()) {
    if (!validateNaturalLanguage(value.language).empty())
      codes.insert(ValidationCode::kStringWithLangInvalidLanguage);
  }
  return codes;
}

// Validate a single value in `attribute`. `attribute` must not be nullptr and
// `value_index` must be a valid index.
std::set<ValidationCode> ValidateValue(const Attribute* attribute,
                                       size_t value_index) {
  if (IsString(attribute->Tag())) {
    std::string values_str;
    attribute->GetValue(&values_str, value_index);
    switch (attribute->Tag()) {
      case ValueTag::textWithoutLanguage:
        return validateTextWithoutLanguage(values_str);
      case ValueTag::nameWithoutLanguage:
        return validateNameWithoutLanguage(values_str);
      case ValueTag::keyword:
        return validateKeyword(values_str);
      case ValueTag::uri:
        return validateUri(values_str);
      case ValueTag::uriScheme:
        return validateUriScheme(values_str);
      case ValueTag::charset:
        return validateCharset(values_str);
      case ValueTag::naturalLanguage:
        return validateNaturalLanguage(values_str);
      case ValueTag::mimeMediaType:
        return validateMimeMediaType(values_str);
      default:
        // There are no validation rules for other strings.
        return {};
    }
  }
  switch (attribute->Tag()) {
    case ValueTag::octetString: {
      std::string value;
      attribute->GetValue(&value, value_index);
      return validateOctetString(value);
    }
    case ValueTag::dateTime: {
      DateTime value;
      attribute->GetValue(&value, value_index);
      return validateDateTime(value);
    }
    case ValueTag::resolution: {
      Resolution value;
      attribute->GetValue(&value, value_index);
      return validateResolution(value);
    }
    case ValueTag::rangeOfInteger: {
      RangeOfInteger value;
      attribute->GetValue(&value, value_index);
      return validateRangeOfInteger(value);
    }
    case ValueTag::textWithLanguage: {
      StringWithLanguage value;
      attribute->GetValue(&value, value_index);
      return validateTextWithLanguage(value);
    }
    case ValueTag::nameWithLanguage: {
      StringWithLanguage value;
      attribute->GetValue(&value, value_index);
      return validateNameWithLanguage(value);
    }
    default:
      // Other types does not need validation.
      return {};
  }
}

struct ValidationResult {
  bool no_errors = true;
  bool keep_going = true;
};

ValidationResult operator&&(ValidationResult vr1, ValidationResult vr2) {
  ValidationResult result;
  result.keep_going = vr1.keep_going && vr2.keep_going;
  result.no_errors = vr1.no_errors && vr2.no_errors;
  return result;
}

ValidationResult ValidateCollections(
    const std::vector<const Collection*>& colls,
    ErrorsLog& log,
    AttrPath& path);

ValidationResult ValidateAttribute(const Attribute* attr,
                                   ErrorsLog& log,
                                   AttrPath& path) {
  ValidationResult result;
  std::set<ValidationCode> name_errors = validateKeyword(attr->Name());
  if (!name_errors.empty()) {
    result.no_errors = false;
    result.keep_going =
        log.AddValidationError(path, AttrError(std::move(name_errors)));
    if (!result.keep_going)
      return result;
  }
  const size_t values_count = attr->Size();
  if (attr->Tag() == ValueTag::collection) {
    std::vector<const Collection*> colls(values_count);
    for (size_t i = 0; i < values_count; ++i) {
      colls[i] = attr->GetCollection(i);
    }
    result = result && ValidateCollections(colls, log, path);
  } else {
    for (size_t i = 0; i < values_count; ++i) {
      std::set<ValidationCode> value_errors = ValidateValue(attr, i);
      if (!value_errors.empty()) {
        result.no_errors = false;
        result.keep_going =
            log.AddValidationError(path, AttrError(i, std::move(value_errors)));
        if (!result.keep_going)
          return result;
      }
    }
  }
  return result;
}

ValidationResult ValidateCollections(
    const std::vector<const Collection*>& colls,
    ErrorsLog& log,
    AttrPath& path) {
  ValidationResult result;
  for (size_t icoll = 0; icoll < colls.size(); ++icoll) {
    const Collection* coll = colls[icoll];
    std::vector<const Attribute*> attrs = coll->GetAllAttributes();
    for (const Attribute* attr : attrs) {
      path.PushBack(icoll, attr->Name());
      result = result && ValidateAttribute(attr, log, path);
      path.PopBack();
      if (!result.keep_going)
        return result;
    }
  }
  return result;
}

}  // namespace

bool Validate(const Frame& frame, ErrorsLog& log) {
  ValidationResult result;
  for (GroupTag group_tag : kGroupTags) {
    std::vector<const Collection*> groups = frame.GetGroups(group_tag);
    for (size_t index = 0; index < groups.size(); ++index) {
      AttrPath path(group_tag);
      std::vector<const Attribute*> attrs = groups[index]->GetAllAttributes();
      for (const Attribute* attr : attrs) {
        path.PushBack(index, attr->Name());
        result = result && ValidateAttribute(attr, log, path);
        path.PopBack();
        if (!result.keep_going)
          return result.no_errors;
      }
    }
  }
  return result.no_errors;
}

}  // namespace ipp