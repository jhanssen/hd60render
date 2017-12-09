#ifndef OPTIONS_H
#define OPTIONS_H

#include <string>
#include <variant>
#include <vector>
#include <optional>
#include <exception>
#include <type_traits>
#include <unordered_map>
#include <cstdlib>

class Options
{
public:
    typedef std::variant<bool, long long int, double, std::string> Value;

    static Options parse(int argc, char** argv);

    bool enabled(const char* arg) const;

    bool has(const char* arg) const;
    template<typename Type>
    bool has(const char* arg) const;

    std::optional<Value> get(const char* arg) const;
    template<typename Type>
    std::optional<Type> get(const char* arg) const;

    Value get(const char* arg, const Value& defaultValue) const;
    template<typename Type,
             typename std::enable_if<std::is_arithmetic<Type>::value, Type>::type = 0>
    Type get(const char* arg, Type defaultValue) const;
    template<typename Type,
             typename std::enable_if<!std::is_arithmetic<Type>::value, Type>::type = 0>
    Type get(const char* arg, const Type& defaultValue) const;

    struct Standalones
    {
        size_t size() const;
        template<typename Type>
        bool is(size_t idx) const;
        std::optional<Value> at(size_t idx) const;
        template<typename Type>
        std::optional<Type> at(size_t idx) const;

    private:
        Standalones() { }

        std::vector<Value> standalones;

        friend class Options;
    };
    const Standalones& standalones() { return standaloneValues; }

private:
    typedef std::unordered_map<std::string, Value> Values;

    struct Option
    {
        Option(const char* arg);

        std::string longOption, shortOption;
        bool hasShort;

        Values::const_iterator find(const Values& v) const;
    };

    Options(int argc, char** argv);

    Values values;
    Standalones standaloneValues;
};

Options::Option::Option(const char* arg)
    : longOption(arg), hasShort(false)
{
    // see if we have an '&'
    auto idx = longOption.find('&');
    if (idx != std::string::npos) {
        // remove the & and make the next char be our short option
        longOption.erase(longOption.begin() + idx);
        if (idx < longOption.size()) {
            hasShort = true;
            shortOption = longOption[idx];
        }
    }
}

Options::Values::const_iterator Options::Option::find(const Values& values) const
{
    auto v = values.find(longOption);
    if (v == values.end()) {
        if (hasShort)
            return values.find(shortOption);
    }
    return v;
}

Options::Options(int argc, char** argv)
{
    // 1. any argument starting with '-' is an option
    // 2. if the option is preceeded by a single '-' then any characters in the argument string is a separate option
    // 3. if the argument contains an = then what comes after is the value for the option
    // 4. if the next argument after an option starts with '-' then that is also an option
    // 5. if the next argument after an option does not start with '-' then that is the value for the option
    // 6. any other arguments are stand-alone arguments
    // 7. any arguments after -- are always stand-alone arguments

    bool dashdash = false;
    std::string hadopt;

    auto makeValue = [](const std::string& valstr) -> Value {
        // empty values are true booleans
        if (valstr.empty()) {
            return Value{true};
        }
        if (valstr == "true" || valstr == "yes" || valstr == "on") {
            return Value{true};
        } else if (valstr == "false" || valstr == "no" || valstr == "off") {
            return Value{false};
        }
        // check for numeric values
        char* end;
        long long int ival = std::strtoll(valstr.c_str(), &end, 10);
        if (*end == '\0') {
            return Value{ival};
        }
        double dval = std::strtod(valstr.c_str(), &end);
        if (*end == '\0') {
            return Value{dval};
        }
        return Value{valstr};
    };

    auto handleOption = [&makeValue, &hadopt, this](const std::string& optstr,
                                                    const std::optional<std::string> valstr) {
        if (!hadopt.empty()) {
            values[hadopt] = Value{true};
            hadopt.clear();
        }

        // do we start with "no-"? if so then we're disabled
        if (optstr.substr(0, 3) == "no-") {
            Value v = makeValue(valstr.value_or(std::string()));
            bool* bv = std::get_if<bool>(&v);
            if (!bv) {
                // bad option
                return;
            }
            values[optstr.substr(3)] = Value{!*bv};
        } else {
            if (valstr) {
                values[optstr] = makeValue(*valstr);
            } else {
                hadopt = optstr;
            }
        }
    };

    for (int i = 1; i < argc; ++i) {
        if (dashdash) {
            standaloneValues.standalones.push_back(argv[i]);
        } else {
            if (argv[i][0] == '-') {
                if (!hadopt.empty()) {
                    values[hadopt] = Value{true};
                    hadopt.clear();
                }

                if (argv[i][1] == '-' && argv[i][2] == '\0') {
                    dashdash = true;
                } else {
                    // skip all '-'
                    int idx = 1;
                    while (argv[i][idx] == '-')
                        ++idx;
                    if (argv[i][idx] == '\0') {
                        // bad option
                        continue;
                    }
                    std::string optstr = argv[i] + idx, valstr;
                    // do we have an '='? if so then that's our value, otherwise our value is 'true' or the next argument
                    const size_t eqpos = optstr.find('=');
                    if (eqpos != std::string::npos) {
                        valstr = optstr.substr(eqpos + 1);
                        optstr = optstr.substr(0, eqpos);
                    }
                    if (optstr.empty()) {
                        // bad option
                        continue;
                    }
                    if (idx == 1) {
                        // single dash, potentially multiple options
                        if (eqpos != std::string::npos) {
                            // special case, just take the first option if we have an equal value
                            handleOption(optstr.substr(0, 1), valstr);
                        } else {
                            for (size_t o = 0; o < optstr.size(); ++o) {
                                handleOption(optstr.substr(o, 1), {});
                            }
                        }
                    } else {
                        if (eqpos != std::string::npos) {
                            handleOption(optstr, valstr);
                        } else {
                            handleOption(optstr, {});
                        }
                    }
                }
            } else {
                if (hadopt.empty()) {
                    standaloneValues.standalones.push_back(argv[i]);
                } else {
                    values[hadopt] = makeValue(argv[i]);
                    hadopt.clear();
                }
            }
        }
    }

    if (!hadopt.empty()) {
        values[hadopt] = Value{true};
        hadopt.clear();
    }
}

Options Options::parse(int argc, char** argv)
{
    return Options(argc, argv);
}

bool Options::has(const char* arg) const
{
    Option opt(arg);
    return values.count(opt.longOption) > 0 || (opt.hasShort && values.count(opt.shortOption) > 0);
}

template<typename Type>
bool Options::has(const char* arg) const
{
    Option opt(arg);
    auto v = opt.find(values);
    if (v == values.end())
        return false;
    return std::holds_alternative<Type>(v->second);
}

template<>
bool Options::has<int>(const char* arg) const
{
    return has<long long int>(arg);
}

std::optional<Options::Value> Options::get(const char* arg) const
{
    Option opt(arg);
    auto v = opt.find(values);
    if (v == values.end())
        return {};
    return v->second;
}

template<typename Type>
std::optional<Type> Options::get(const char* arg) const
{
    Option opt(arg);
    auto v = opt.find(values);
    if (v == values.end())
        return {};
    if (const Type* ptr = std::get_if<Type>(&v->second))
        return *ptr;
    return {};
}

template<>
std::optional<int> Options::get<int>(const char* arg) const
{
    return get<long long int>(arg);
}

Options::Value Options::get(const char* arg, const Value& defaultValue) const
{
    Option opt(arg);
    auto v = opt.find(values);
    if (v == values.end())
        return defaultValue;
    return v->second;
}

template<typename Type,
         typename std::enable_if<std::is_arithmetic<Type>::value, Type>::type = 0>
Type Options::get(const char* arg, Type defaultValue) const
{
    Option opt(arg);
    auto v = opt.find(values);
    if (v == values.end())
        return defaultValue;
    if (const Type* ptr = std::get_if<Type>(&v->second))
        return *ptr;
    return defaultValue;
}

template<typename Type,
         typename std::enable_if<!std::is_arithmetic<Type>::value, Type>::type = 0>
Type Options::get(const char* arg, const Type& defaultValue) const
{
    Option opt(arg);
    auto v = opt.find(values);
    if (v == values.end())
        return defaultValue;
    if (const Type* ptr = std::get_if<Type>(&v->second))
        return *ptr;
    return defaultValue;
}

template<>
int Options::get<int>(const char* arg, int defaultValue) const
{
    return get<long long int>(arg, defaultValue);
}

bool Options::enabled(const char* arg) const
{
    Option opt(arg);
    auto v = opt.find(values);
    if (v == values.end())
        return false;
    const bool* ok = std::get_if<bool>(&v->second);
    if (ok)
        return *ok;
    return false;
}

size_t Options::Standalones::size() const
{
    return standalones.size();
}

template<typename Type>
bool Options::Standalones::is(size_t idx) const
{
    if (idx >= standalones.size())
        return false;
    return std::holds_alternative<Type>(standalones[idx]);
}

std::optional<Options::Value> Options::Standalones::at(size_t idx) const
{
    if (idx >= standalones.size())
        return {};
    return standalones[idx];
}

template<typename Type>
std::optional<Type> Options::Standalones::at(size_t idx) const
{
    if (idx >= standalones.size())
        return {};
    if (const Type* ptr = std::get_if<Type>(&standalones[idx]))
        return *ptr;
    return {};
}

template<>
std::optional<int> Options::Standalones::at<int>(size_t idx) const
{
    return at<long long int>(idx);
}

#endif
