#ifndef OPTIONS_H
#define OPTIONS_H

#include <string>
#include <variant>
#include <stdexcept>
#include <exception>
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
    Value value(const char* arg) const;
    template<typename Type>
    Type value(const char* arg) const;

    struct Standalones
    {
        size_t size() const;
        Value at(size_t idx) const;
        template<typename Type>
        Type at(size_t idx) const;

    private:
        Standalones() { }

        std::vector<Value> standalones;

        friend class Options;
    };
    const Standalones& standalones() { return standaloneValues; }

private:
    struct Option
    {
        Option(const char* arg);

        std::string longOption, shortOption;
        bool hasShort;
    };

    Options(int argc, char** argv);

    std::unordered_map<std::string, Value> values;
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

Options::Options(int argc, char** argv)
{
    // 1. any argument starting with '-' is an option
    // 2. if the argument contains an = then what comes after is the value for the option
    // 3. if the next argument after an option starts with '-' then that is also an option
    // 4. if the next argument after an option does not start with '-' then that is the value for the option
    // 5. any other arguments are stand-alone arguments
    // 6. any arguments after -- are always stand-alone arguments

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

    bool dashdash = false;
    std::string hadopt;
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
                    // do we have an '='? if so then that's our value, otherwise our value is 'true'
                    const size_t eqpos = optstr.find('=');
                    if (eqpos != std::string::npos) {
                        valstr = optstr.substr(eqpos + 1);
                        optstr = optstr.substr(0, eqpos);
                    }
                    if (optstr.empty()) {
                        // bad option
                        continue;
                    }
                    // do we start with "no-"? if so then we're disabled
                    if (optstr.substr(0, 3) == "no-") {
                        Value v = makeValue(valstr);
                        bool* bv = std::get_if<bool>(&v);
                        if (!bv) {
                            // bad option
                            continue;
                        }
                        values[optstr.substr(3)] = Value{!*bv};
                    } else {
                        if (eqpos != std::string::npos) {
                            values[optstr] = makeValue(valstr);
                        } else {
                            hadopt = optstr;
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
}

Options Options::parse(int argc, char** argv)
{
    return Options(argc, argv);
}

bool Options::has(const char* arg) const
{
    Option opt(arg);
    return values.count(opt.longOption) > 0 || (opt.hasShort && values.count(opt.shortOption));
}

template<typename Type>
bool Options::has(const char* arg) const
{
    Option opt(arg);
    auto v = values.find(opt.longOption);
    if (v == values.end()) {
        if (opt.hasShort) {
            v = values.find(opt.shortOption);
            if (v == values.end()) {
                return false;
            }
        } else {
            return false;
        }
    }
    return std::holds_alternative<Type>(v->second);
}

template<>
bool Options::has<int>(const char* arg) const
{
    return has<long long int>(arg);
}

Options::Value Options::value(const char* arg) const
{
    Option opt(arg);
    auto v = values.find(opt.longOption);
    if (v == values.end()) {
        if (opt.hasShort) {
            v = values.find(opt.shortOption);
            if (v == values.end()) {
                std::throw_with_nested(std::runtime_error("no such value"));
            }
        } else {
            std::throw_with_nested(std::runtime_error("no such value"));
        }
    }
    return v->second;
}

template<typename Type>
Type Options::value(const char* arg) const
{
    Option opt(arg);
    auto v = values.find(opt.longOption);
    if (v == values.end()) {
        if (opt.hasShort) {
            v = values.find(opt.shortOption);
            if (v == values.end()) {
                std::throw_with_nested(std::runtime_error("no such value"));
            }
        } else {
            std::throw_with_nested(std::runtime_error("no such value"));
        }
    }
    return std::get<Type>(v->second);
}

template<>
int Options::value<int>(const char* arg) const
{
    return static_cast<int>(value<long long int>(arg));
}

bool Options::enabled(const char* arg) const
{
    Option opt(arg);
    auto v = values.find(opt.longOption);
    if (v == values.end()) {
        if (opt.hasShort) {
            v = values.find(opt.shortOption);
            if (v == values.end()) {
                return false;
            }
        } else {
            return false;
        }
    }
    const bool* ok = std::get_if<bool>(&v->second);
    if (ok) {
        return *ok;
    }
    return false;
}

size_t Options::Standalones::size() const
{
    return standalones.size();
}

Options::Value Options::Standalones::at(size_t idx) const
{
    return standalones[idx];
}

template<typename Type>
Type Options::Standalones::at(size_t idx) const
{
    return std::get<Type>(standalones[idx]);
}

template<>
int Options::Standalones::at<int>(size_t idx) const
{
    return static_cast<int>(std::get<long long int>(standalones[idx]));
}

#endif
