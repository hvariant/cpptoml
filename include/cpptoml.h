/**
 * @file cpptoml.h
 * @author Chase Geigle
 * @date May 2013
 */

#ifndef _CPPTOML_H_
#define _CPPTOML_H_

#include <algorithm>
#include <stdexcept>
#if !CPPTOML_HAS_STD_PUT_TIME
#include <array>
#endif
#include <cstdint>
#include <ctime>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <memory>
#if CPPTOML_HAS_STD_REGEX
#include <regex>
#endif
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cpptoml
{

template <class T>
class option
{
  public:
    option() : empty_{true}
    {
        // nothing
    }

    option(T value) : empty_{false}, value_{std::move(value)}
    {
        // nothing
    }

    explicit operator bool() const
    {
        return !empty_;
    }

    const T& operator*() const
    {
        return value_;
    }

  private:
    bool empty_;
    T value_;
};

template <class T>
class value;

class array;
class table;
class table_array;

/**
 * A generic base TOML value used for type erasure.
 */
class base : public std::enable_shared_from_this<base>
{
  public:
    /**
     * Determines if the given TOML element is a value.
     */
    virtual bool is_value() const
    {
        return false;
    }

    /**
     * Determines if the given TOML element is a table.
     */
    virtual bool is_table() const
    {
        return false;
    }

    /**
     * Converts the TOML element into a table.
     */
    std::shared_ptr<table> as_table()
    {
        if (is_table())
            return std::static_pointer_cast<table>(shared_from_this());
        return nullptr;
    }
    /**
     * Determines if the TOML element is an array of "leaf" elements.
     */
    virtual bool is_array() const
    {
        return false;
    }

    /**
     * Converts the TOML element to an array.
     */
    std::shared_ptr<array> as_array()
    {
        if (is_array())
            return std::static_pointer_cast<array>(shared_from_this());
        return nullptr;
    }

    /**
     * Determines if the given TOML element is an array of tables.
     */
    virtual bool is_table_array() const
    {
        return false;
    }

    /**
     * Converts the TOML element into a table array.
     */
    std::shared_ptr<table_array> as_table_array()
    {
        if (is_table_array())
            return std::static_pointer_cast<table_array>(shared_from_this());
        return nullptr;
    }

    /**
     * Prints the TOML element to the given stream.
     */
    virtual void print(std::ostream& stream) const = 0;

    /**
     * Attempts to coerce the TOML element into a concrete TOML value
     * of type T.
     */
    template <class T>
    std::shared_ptr<value<T>> as();
};

template <class T>
struct valid_value
{
    const static bool value
        = std::is_same<T, std::string>::value || std::is_same<T, int64_t>::value
          || std::is_same<T, double>::value || std::is_same<T, bool>::value
          || std::is_same<T, std::tm>::value;
};

/**
 * A concrete TOML value representing the "leaves" of the "tree".
 */
template <class T>
class value : public base
{
  public:
    static_assert(valid_value<T>::value, "invalid value type");

    /**
     * Constructs a value from the given data.
     */
    value(const T& val) : data_{val}
    {
    }

    bool is_value() const override
    {
        return true;
    }

    /**
     * Gets the data associated with this value.
     */
    T& get()
    {
        return data_;
    }

    /**
     * Gets the data associated with this value. Const version.
     */
    const T& get() const
    {
        return data_;
    }

    void print(std::ostream& stream) const override
    {
        stream << data_;
    }

  private:
    T data_;
};

// I don't think I'll ever comprehend why this is needed...
template <>
inline value<std::tm>::value(const std::tm& date)
{
    data_ = date;
}

// specializations for printing nicely
template <>
inline void value<bool>::print(std::ostream& stream) const
{
    if (data_)
        stream << "true";
    else
        stream << "false";
}

template <>
inline void value<std::tm>::print(std::ostream& stream) const
{
#if CPPTOML_HAS_STD_PUT_TIME
    stream << std::put_time(&data_, "%c");
#else
    std::array<char, 100> buf;
    if (std::strftime(&buf[0], 100, "%c", &data_))
        stream << &buf[0] << " UTC";
#endif
}

template <class T>
inline std::shared_ptr<value<T>> base::as()
{
    if (auto v = std::dynamic_pointer_cast<value<T>>(shared_from_this()))
        return v;
    return nullptr;
}

class array : public base
{
  public:
    array() = default;

    template <class InputIterator>
    array(InputIterator begin, InputIterator end)
        : values_{begin, end}
    {
        // nothing
    }

    virtual bool is_array() const override
    {
        return true;
    }

    /**
     * Obtains the array (vector) of base values.
     */
    std::vector<std::shared_ptr<base>>& get()
    {
        return values_;
    }

    /**
     * Obtains the array (vector) of base values. Const version.
     */
    const std::vector<std::shared_ptr<base>>& get() const
    {
        return values_;
    }

    std::shared_ptr<base> at(size_t idx) const
    {
        return values_.at(idx);
    }

    /**
     * Obtains an array of value<T>s. Note that elements may be
     * nullptr if they cannot be converted to a value<T>.
     */
    template <class T>
    std::vector<std::shared_ptr<value<T>>> array_of() const
    {
        std::vector<std::shared_ptr<value<T>>> result(values_.size());

        std::transform(values_.begin(), values_.end(), result.begin(),
                       [&](std::shared_ptr<base> v)
                       {
            return v->as<T>();
        });

        return result;
    }

    /**
     * Obtains an array of arrays. Note that elements may be nullptr
     * if they cannot be converted to a array.
     */
    std::vector<std::shared_ptr<array>> nested_array() const
    {
        std::vector<std::shared_ptr<array>> result(values_.size());

        std::transform(values_.begin(), values_.end(), result.begin(),
                       [&](std::shared_ptr<base> v)
                       {
            if (v->is_array())
                return std::static_pointer_cast<array>(v);
            return std::shared_ptr<array>{};
        });

        return result;
    }

    virtual void print(std::ostream& stream) const override
    {
        stream << "[ ";
        auto it = values_.begin();
        while (it != values_.end())
        {
            (*it)->print(stream);
            if (++it != values_.end())
                stream << ", ";
        }
        stream << " ]";
    }

  private:
    std::vector<std::shared_ptr<base>> values_;
};

class table;

class table_array : public base
{
    friend class table;

  public:
    virtual bool is_table_array() const override
    {
        return true;
    }

    std::vector<std::shared_ptr<table>>& get()
    {
        return array_;
    }

    void print(std::ostream& stream) const override
    {
        print(stream, 0, "");
    }

  private:
    void print(std::ostream& stream, size_t depth,
               const std::string& key) const;
    std::vector<std::shared_ptr<table>> array_;
};

/**
 * Represents a TOML keytable.
 */
class table : public base
{
  public:
    friend class table_array;
    /**
     * tables can be iterated over.
     */
    using iterator
        = std::unordered_map<std::string, std::shared_ptr<base>>::iterator;

    /**
     * tables can be iterated over. Const version.
     */
    using const_iterator
        = std::unordered_map<std::string,
                             std::shared_ptr<base>>::const_iterator;

    iterator begin()
    {
        return map_.begin();
    }

    const_iterator begin() const
    {
        return map_.begin();
    }

    iterator end()
    {
        return map_.end();
    }

    const_iterator end() const
    {
        return map_.end();
    }

    bool is_table() const override
    {
        return true;
    }

    /**
     * Determines if this key table contains the given key.
     */
    bool contains(const std::string& key) const
    {
        return map_.find(key) != map_.end();
    }

    /**
     * Determines if this key table contains the given key. Will
     * resolve "qualified keys". Qualified keys are the full access
     * path separated with dots like "grandparent.parent.child".
     */
    bool contains_qualified(const std::string& key) const
    {
        return resolve_qualified(key);
    }

    /**
     * Obtains the base for a given key.
     * @throw std::out_of_range if the key does not exist
     */
    std::shared_ptr<base> get(const std::string& key) const
    {
        return map_.at(key);
    }

    /**
     * Obtains the base for a given key. Will resolve "qualified
     * keys". Qualified keys are the full access path separated with
     * dots like "grandparent.parent.child".
     *
     * @throw std::out_of_range if the key does not exist
     */
    std::shared_ptr<base> get_qualified(const std::string& key) const
    {
        std::shared_ptr<base> p;
        resolve_qualified(key, &p);
        return p;
    }

    /**
     * Obtains a table for a given key, if possible.
     */
    std::shared_ptr<table> get_table(const std::string& key) const
    {
        if (contains(key) && get(key)->is_table())
            return std::static_pointer_cast<table>(get(key));
        return nullptr;
    }

    /**
     * Obtains a table for a given key, if possible. Will resolve
     * "qualified keys".
     */
    std::shared_ptr<table> get_table_qualified(const std::string& key) const
    {
        if (contains_qualified(key) && get_qualified(key)->is_table())
            return std::static_pointer_cast<table>(get_qualified(key));
        return nullptr;
    }

    /**
     * Obtains an array for a given key.
     */
    std::shared_ptr<array> get_array(const std::string& key) const
    {
        if (!contains(key))
            return nullptr;
        return get(key)->as_array();
    }

    /**
     * Obtains an array for a given key. Will resolve "qualified keys".
     */
    std::shared_ptr<array> get_array_qualified(const std::string& key) const
    {
        if (!contains_qualified(key))
            return nullptr;
        return get_qualified(key)->as_array();
    }

    /**
     * Obtains a table_array for a given key, if possible.
     */
    std::shared_ptr<table_array> get_table_array(const std::string& key) const
    {
        if (!contains(key))
            return nullptr;
        return get(key)->as_table_array();
    }

    /**
     * Obtains a table_array for a given key, if possible. Will resolve
     * "qualified keys".
     */
    std::shared_ptr<table_array>
        get_table_array_qualified(const std::string& key) const
    {
        if (!contains_qualified(key))
            return nullptr;
        return get_qualified(key)->as_table_array();
    }

    /**
     * Helper function that attempts to get a value corresponding
     * to the template parameter from a given key.
     */
    template <class T>
    option<T> get_as(const std::string& key) const
    {
        try
        {
            if (auto v = get(key)->as<T>())
                return {v->value()};
            else
                return {};
        }
        catch (const std::out_of_range&)
        {
            return {};
        }
    }

    /**
     * Helper function that attempts to get a value corresponding
     * to the template parameter from a given key. Will resolve "qualified
     * keys".
     */
    template <class T>
    option<T> get_qualified_as(const std::string& key) const
    {
        try
        {
            if (auto v = get_qualified(key)->as<T>())
                return {v->value()};
            else
                return {};
        }
        catch (const std::out_of_range&)
        {
            return {};
        }
    }

    /**
     * Adds an element to the keytable.
     */
    void insert(const std::string& key, const std::shared_ptr<base>& value)
    {
        map_[key] = value;
    }

    /**
     * Convenience shorthand for adding a simple element to the
     * keytable.
     */
    template <class T>
    void insert(const std::string& key, T&& val,
                typename std::enable_if<valid_value<T>::value>::type* = 0)
    {
        insert(key, std::make_shared<value<T>>(val));
    }

    friend std::ostream& operator<<(std::ostream& stream, const table& table);

    void print(std::ostream& stream) const override
    {
        print(stream, 0);
    }

  private:
    std::vector<std::string> split(const std::string& value,
                                   char separator) const
    {
        std::vector<std::string> result;
        std::string::size_type p = 0;
        std::string::size_type q;
        while ((q = value.find(separator, p)) != std::string::npos)
        {
            result.emplace_back(value, p, q - p);
            p = q + 1;
        }
        result.emplace_back(value, p);
        return result;
    }

    // If output parameter p is specified, fill it with the pointer to the
    // specified entry and throw std::out_of_range if it couldn't be found.
    //
    // Otherwise, just return true if the entry could be found or false
    // otherwise and do not throw.
    bool resolve_qualified(const std::string& key,
                           std::shared_ptr<base>* p = nullptr) const
    {
        auto parts = split(key, '.');
        auto last_key = parts.back();
        parts.pop_back();

        auto table = this;
        for (const auto& part : parts)
        {
            table = table->get_table(part).get();
            if (!table)
            {
                if (!p)
                    return false;

                throw std::out_of_range{key + " is not a valid key"};
            }
        }

        if (!p)
            return table->map_.count(last_key) != 0;

        *p = table->map_.at(last_key);
        return true;
    }

    void print(std::ostream& stream, size_t depth) const
    {
        for (auto& p : map_)
        {
            if (p.second->is_table_array())
            {
                auto ga = std::dynamic_pointer_cast<table_array>(p.second);
                ga->print(stream, depth, p.first);
            }
            else
            {
                stream << std::string(depth, '\t') << p.first << " = ";
                if (p.second->is_table())
                {
                    auto g = static_cast<table*>(p.second.get());
                    stream << '\n';
                    g->print(stream, depth + 1);
                }
                else
                {
                    p.second->print(stream);
                    stream << '\n';
                }
            }
        }
    }
    std::unordered_map<std::string, std::shared_ptr<base>> map_;
};

inline void table_array::print(std::ostream& stream, size_t depth,
                               const std::string& key) const
{
    for (auto g : array_)
    {
        stream << std::string(depth, '\t') << "[[" << key << "]]\n";
        g->print(stream, depth + 1);
    }
}

inline std::ostream& operator<<(std::ostream& stream, const table& table)
{
    table.print(stream);
    return stream;
}

/**
 * Exception class for all TOML parsing errors.
 */
class parse_exception : public std::runtime_error
{
  public:
    parse_exception(const std::string& err) : std::runtime_error{err}
    {
    }

    parse_exception(const std::string& err, std::size_t line_number)
        : std::runtime_error{err + " at line " + std::to_string(line_number)}
    {
    }
};

/**
 * The parser class.
 */
class parser
{
  public:
    /**
     * Parsers are constructed from streams.
     */
    parser(std::istream& stream) : input_(stream)
    {
    }

    parser& operator=(const parser& parser) = delete;

    /**
     * Parses the stream this parser was created on until EOF.
     * @throw parse_exception if there are errors in parsing
     */
    table parse()
    {
        table root;

        table* curr_table = &root;

        while (std::getline(input_, line_))
        {
            line_number_++;
            auto it = line_.begin();
            auto end = line_.end();
            consume_whitespace(it, end);
            if (it == end || *it == '#')
                continue;
            if (*it == '[')
            {
                curr_table = &root;
                parse_table(it, end, curr_table);
            }
            else
            {
                parse_key_value(it, end, curr_table);
            }
        }
        tables_.clear();
        return root;
    }

  private:
#if defined _MSC_VER
    __declspec(noreturn)
#elif defined __GNUC__
    __attribute__((noreturn))
#endif
        void throw_parse_exception(const std::string& err)
    {
        throw parse_exception{err, line_number_};
    }

    void parse_table(std::string::iterator& it,
                     const std::string::iterator& end, table*& curr_table)
    {
        // remove the beginning keytable marker
        ++it;
        if (it == end)
            throw_parse_exception("Unexpected end of table");
        if (*it == '[')
            parse_table_array(it, end, curr_table);
        else
            parse_single_table(it, end, curr_table);
    }

    void parse_single_table(std::string::iterator& it,
                            const std::string::iterator& end,
                            table*& curr_table)
    {
        auto ob = std::find(it, end, '[');
        if (ob != end)
            throw_parse_exception("Cannot have [ in table name");

        auto kg_end = std::find(it, end, ']');
        if (it == kg_end)
            throw_parse_exception("Empty table");

        std::string table_name{it, kg_end};
        if (tables_.find(table_name) != tables_.end())
            throw_parse_exception("Duplicate table");

        if (std::find_if(table_name.begin(), table_name.end(), [](char c)
                         {
                return c == ' ' || c == '\t';
            }) != table_name.end())
        {
            throw parse_exception("Table name " + table_name
                                  + " cannot have whitespace");
        }

        tables_.insert({it, kg_end});
        while (it != kg_end)
        {
            auto dot = std::find(it, kg_end, '.');
            // get the key part
            std::string part{it, dot};
            if (part.empty())
                throw_parse_exception("Empty keytable part");
            it = dot;
            if (it != kg_end)
                ++it;

            if (curr_table->contains(part))
            {
                auto b = curr_table->get(part);
                if (b->is_table())
                    curr_table = static_cast<table*>(b.get());
                else if (b->is_table_array())
                    curr_table = std::static_pointer_cast<table_array>(b)
                                     ->get()
                                     .back()
                                     .get();
                else
                    throw_parse_exception("Keytable already exists as a value");
            }
            else
            {
                curr_table->insert(part, std::make_shared<table>());
                curr_table = static_cast<table*>(curr_table->get(part).get());
            }
        }
        ++it;
        consume_whitespace(it, end);
        eol_or_comment(it, end);
    }

    void parse_table_array(std::string::iterator& it,
                           const std::string::iterator& end, table*& curr_table)
    {
        ++it;
        auto ob = std::find(it, end, '[');
        if (ob != end)
            throw_parse_exception("Cannot have [ in keytable name");
        auto kg_end = std::find(it, end, ']');
        if (kg_end == end)
            throw_parse_exception("Unterminated keytable array");
        if (it == kg_end)
            throw_parse_exception("Empty keytable");
        auto kga_end = kg_end;
        if (*++kga_end != ']')
            throw_parse_exception("Invalid keytable array specifier");
        while (it != kg_end)
        {
            auto dot = std::find(it, kg_end, '.');
            std::string part{it, dot};
            if (part.empty())
                throw_parse_exception("Empty keytable part");
            it = dot;
            if (it != kg_end)
                ++it;
            if (curr_table->contains(part))
            {
                auto b = curr_table->get(part);
                if (it == kg_end)
                {
                    if (!b->is_table_array())
                        throw_parse_exception("Expected keytable array");
                    auto v = std::static_pointer_cast<table_array>(b);
                    v->get().push_back(std::make_shared<table>());
                    curr_table = v->get().back().get();
                }
                else
                {
                    if (b->is_table())
                        curr_table = static_cast<table*>(b.get());
                    else if (b->is_table_array())
                        curr_table = std::static_pointer_cast<table_array>(b)
                                         ->get()
                                         .back()
                                         .get();
                    else
                        throw_parse_exception(
                            "Keytable already exists as a value");
                }
            }
            else
            {
                if (it == kg_end)
                {
                    curr_table->insert(part, std::make_shared<table_array>());
                    auto arr = std::static_pointer_cast<table_array>(
                        curr_table->get(part));
                    arr->get().push_back(std::make_shared<table>());
                    curr_table = arr->get().back().get();
                }
                else
                {
                    curr_table->insert(part, std::make_shared<table>());
                    curr_table
                        = static_cast<table*>(curr_table->get(part).get());
                }
            }
        }
    }

    void parse_key_value(std::string::iterator& it, std::string::iterator& end,
                         table*& curr_table)
    {
        std::string key = parse_key(it, end);
        if (curr_table->contains(key))
            throw_parse_exception("Key " + key + " already present");
        if (*it != '=')
            throw_parse_exception("Value must follow after a '='");
        ++it;
        consume_whitespace(it, end);
        curr_table->insert(key, parse_value(it, end));
        consume_whitespace(it, end);
        eol_or_comment(it, end);
    }

    std::string parse_key(std::string::iterator& it,
                          const std::string::iterator& end)
    {
        consume_whitespace(it, end);
        if (*it == '"')
        {
            return parse_quoted_key(it, end);
        }
        else
        {
            return parse_bare_key(it, end);
        }
    }

    std::string parse_bare_key(std::string::iterator& it,
                                const std::string::iterator& end)
    {
        auto eq = std::find(it, end, '=');
        auto key_end = eq;
        --key_end;
        consume_backwards_whitespace(key_end, it);
        ++key_end;
        std::string key{it, key_end};

        if (std::find(it, key_end, '#') != key_end)
        {
            throw_parse_exception("Key " + key + " cannot contain #");
        }

        if (std::find_if(it, key_end, [](char c)
                         {
                return c == ' ' || c == '\t';
            }) != key_end)
        {
            throw_parse_exception("Key " + key + " cannot contain whitespace");
        }

        it = eq;
        consume_whitespace(it, end);
        return key;
    }

    std::string parse_quoted_key(std::string::iterator& it,
                                 const std::string::iterator& end)
    {
        return string_literal(it, end);
    }

    enum class parse_type
    {
        STRING = 1,
        DATE,
        INT,
        FLOAT,
        BOOL,
        ARRAY
    };

    std::shared_ptr<base> parse_value(std::string::iterator& it,
                                      std::string::iterator& end)
    {
        parse_type type = determine_value_type(it, end);
        switch (type)
        {
            case parse_type::STRING:
                return parse_string(it, end);
            case parse_type::DATE:
                return parse_date(it, end);
            case parse_type::INT:
            case parse_type::FLOAT:
                return parse_number(it, end);
            case parse_type::BOOL:
                return parse_bool(it, end);
            case parse_type::ARRAY:
                return parse_array(it, end);
            default:
                throw_parse_exception("Failed to parse value");
        }
    }

    parse_type determine_value_type(const std::string::iterator& it,
                                    const std::string::iterator& end)
    {
        if (*it == '"')
        {
            return parse_type::STRING;
        }
        else if (is_date(it, end))
        {
            return parse_type::DATE;
        }
        else if (is_number(*it) || *it == '-')
        {
            return determine_number_type(it, end);
        }
        else if (*it == 't' || *it == 'f')
        {
            return parse_type::BOOL;
        }
        else if (*it == '[')
        {
            return parse_type::ARRAY;
        }
        throw_parse_exception("Failed to parse value type");
    }

    parse_type determine_number_type(const std::string::iterator& it,
                                     const std::string::iterator& end)
    {
        // determine if we are an integer or a float
        auto check_it = it;
        if (*check_it == '-')
            ++check_it;
        while (check_it != end && is_number(*check_it))
            ++check_it;
        if (check_it != end && *check_it == '.')
        {
            ++check_it;
            while (check_it != end && is_number(*check_it))
                ++check_it;
            return parse_type::FLOAT;
        }
        else
        {
            return parse_type::INT;
        }
    }

    std::shared_ptr<value<std::string>>
        parse_string(std::string::iterator& it,
                     const std::string::iterator& end)
    {
        return std::make_shared<value<std::string>>(string_literal(it, end));
    }

    std::string string_literal(std::string::iterator& it, const std::string::iterator& end)
    {
        ++it;
        std::string val;
        while (it != end)
        {
            // handle escaped characters
            if (*it == '\\')
            {
                val += parse_escape_code(it, end);
            }
            else if (*it == '"')
            {
                ++it;
                consume_whitespace(it, end);
                return val;
            }
            else
            {
                val += *it++;
            }
        }
        throw_parse_exception("Unterminated string literal");
    }

    char parse_escape_code(std::string::iterator& it,
                           const std::string::iterator& end)
    {
        ++it;
        if (it == end)
            throw_parse_exception("Invalid escape sequence");
        char value;
        if (*it == 'b')
        {
            value = '\b';
        }
        else if (*it == 't')
        {
            value = '\t';
        }
        else if (*it == 'n')
        {
            value = '\n';
        }
        else if (*it == 'f')
        {
            value = '\f';
        }
        else if (*it == 'r')
        {
            value = '\r';
        }
        else if (*it == '"')
        {
            value = '"';
        }
        else if (*it == '/')
        {
            value = '/';
        }
        else if (*it == '\\')
        {
            value = '\\';
        }
        else
        {
            throw_parse_exception("Invalid escape sequence");
        }
        ++it;
        return value;
    }

    std::shared_ptr<base> parse_number(std::string::iterator& it,
                                       const std::string::iterator& end)
    {
        // determine if we are an integer or a float
        auto check_it = it;
        if (*check_it == '-')
            ++check_it;
        while (check_it != end && is_number(*check_it))
            ++check_it;
        if (check_it != end && *check_it == '.')
        {
            ++check_it;
            if (check_it == end)
                throw_parse_exception("Floats must have trailing digits");
            while (check_it != end && is_number(*check_it))
                ++check_it;
            return parse_float(it, check_it);
        }
        else
        {
            return parse_int(it, check_it);
        }
    }

    std::shared_ptr<value<int64_t>> parse_int(std::string::iterator& it,
                                              const std::string::iterator& end)
    {
        std::string v{it, end};
        it = end;
        return std::make_shared<value<int64_t>>(std::stoll(v));
    }

    std::shared_ptr<value<double>> parse_float(std::string::iterator& it,
                                               const std::string::iterator& end)
    {
        std::string v{it, end};
        it = end;
        return std::make_shared<value<double>>(std::stod(v));
    }

    std::shared_ptr<value<bool>> parse_bool(std::string::iterator& it,
                                            const std::string::iterator& end)
    {
        auto boolend = std::find_if(it, end, [](char c)
                                    {
            return c == ' ' || c == '\t' || c == '#';
        });
        std::string v{it, boolend};
        it = boolend;
        if (v == "true")
            return std::make_shared<value<bool>>(true);
        else if (v == "false")
            return std::make_shared<value<bool>>(false);
        else
            throw_parse_exception("Attempted to parse invalid boolean value");
    }

    std::shared_ptr<value<std::tm>> parse_date(std::string::iterator& it,
                                               const std::string::iterator& end)
    {
        auto date_end = std::find_if(it, end, [this](char c)
                                     {
            return !is_number(c) && c != 'T' && c != 'Z' && c != ':'
                   && c != '-';
        });
        std::string to_match{it, date_end};
        it = date_end;

#if CPPTOML_HAS_STD_REGEX
        std::regex pattern{
            "(\\d{4})-(\\d{2})-(\\d{2})T(\\d{2}):(\\d{2}):(\\d{2})Z"};
        std::match_results<std::string::const_iterator> results;
        std::regex_match(to_match, results, pattern);

        // populate extracted values
        std::tm date;
        std::memset(&date, '\0', sizeof(date));
        date.tm_year = stoi(results[1]) - 1900;
        date.tm_mon = stoi(results[2]) - 1;
        date.tm_mday = stoi(results[3]);
        date.tm_hour = stoi(results[4]);
        date.tm_min = stoi(results[5]);
        date.tm_sec = stoi(results[6]);
#else
        int year;
        int month;
        int day;
        int hour;
        int min;
        int sec;
        std::sscanf(to_match.c_str(), "%d-%d-%dT%d:%d:%dZ", &year, &month, &day,
                    &hour, &min, &sec);

        // populate extracted values
        std::tm date;
        std::memset(&date, '\0', sizeof(date));
        date.tm_year = year - 1900;
        date.tm_mon = month - 1;
        date.tm_mday = day;
        date.tm_hour = hour;
        date.tm_min = min;
        date.tm_sec = sec;
#endif

        return std::make_shared<value<std::tm>>(date);
    }

    std::shared_ptr<base> parse_array(std::string::iterator& it,
                                      std::string::iterator& end)
    {
        // this gets ugly because of the "homogeneity" restriction:
        // arrays can either be of only one type, or contain arrays
        // (each of those arrays could be of different types, though)
        //
        // because of the latter portion, we don't really have a choice
        // but to represent them as arrays of base values...
        ++it;

        // ugh---have to read the first value to determine array type...
        skip_whitespace_and_comments(it, end);

        // edge case---empty array
        if (*it == ']')
        {
            ++it;
            return std::make_shared<array>();
        }

        auto val_end = std::find_if(it, end, [](char c)
                                    {
            return c == ',' || c == ']' || c == '#';
        });
        parse_type type = determine_value_type(it, val_end);
        switch (type)
        {
            case parse_type::STRING:
                return parse_value_array<std::string>(it, end);
            case parse_type::INT:
                return parse_value_array<int64_t>(it, end);
            case parse_type::FLOAT:
                return parse_value_array<double>(it, end);
            case parse_type::DATE:
                return parse_value_array<std::tm>(it, end);
            case parse_type::ARRAY:
                return parse_nested_array(it, end);
            default:
                throw_parse_exception("Unable to parse array");
        }
    }

    template <class Value>
    std::shared_ptr<array> parse_value_array(std::string::iterator& it,
                                             std::string::iterator& end)
    {
        auto arr = std::make_shared<array>();
        while (it != end && *it != ']')
        {
            auto value = parse_value(it, end);
            if (auto v = value->as<Value>())
                arr->get().push_back(value);
            else
                throw_parse_exception("Arrays must be heterogeneous");
            skip_whitespace_and_comments(it, end);
            if (*it != ',')
                break;
            ++it;
            skip_whitespace_and_comments(it, end);
        }
        if (it != end)
            ++it;
        return arr;
    }

    std::shared_ptr<array> parse_nested_array(std::string::iterator& it,
                                              std::string::iterator& end)
    {
        auto arr = std::make_shared<array>();
        while (it != end && *it != ']')
        {
            arr->get().push_back(parse_array(it, end));
            skip_whitespace_and_comments(it, end);
            if (*it != ',')
                break;
            ++it;
            skip_whitespace_and_comments(it, end);
        }
        if (it != end)
            ++it;
        return arr;
    }

    void skip_whitespace_and_comments(std::string::iterator& start,
                                      std::string::iterator& end)
    {
        consume_whitespace(start, end);
        while (start == end || *start == '#')
        {
            if (!std::getline(input_, line_))
                throw_parse_exception("Unclosed array");
            line_number_++;
            start = line_.begin();
            end = line_.end();
            consume_whitespace(start, end);
        }
    }

    void consume_whitespace(std::string::iterator& it,
                            const std::string::iterator& end)
    {
        while (it != end && (*it == ' ' || *it == '\t'))
            ++it;
    }

    void consume_backwards_whitespace(std::string::iterator& back,
                                      const std::string::iterator& front)
    {
        while (back != front && (*back == ' ' || *back == '\t'))
            --back;
    }

    void eol_or_comment(const std::string::iterator& it,
                        const std::string::iterator& end)
    {
        if (it != end && *it != '#')
            throw_parse_exception("Unidentified trailing character "
                                  + std::string{*it}
                                  + "---did you forget a '#'?");
    }

    bool is_number(char c)
    {
        return c >= '0' && c <= '9';
    }

    bool is_date(const std::string::iterator& it,
                 const std::string::iterator& end)
    {
        auto date_end = std::find_if(it, end, [this](char c)
                                     {
            return !is_number(c) && c != 'T' && c != 'Z' && c != ':'
                   && c != '-';
        });
        std::string to_match{it, date_end};
#if CPPTOML_HAS_STD_REGEX
        std::regex pattern{"\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z"};
        return std::regex_match(to_match, pattern);
#else
        int year;
        int month;
        int day;
        int hour;
        int min;
        int sec;
        return to_match.length() == 20 && to_match[4] == '-'
               && to_match[7] == '-' && to_match[10] == 'T'
               && to_match[13] == ':' && to_match[16] == ':'
               && to_match[19] == 'Z'
               && std::sscanf(to_match.c_str(), "%d-%d-%dT%d:%d:%dZ", &year,
                              &month, &day, &hour, &min, &sec) == 6;
#endif
    }

    std::istream& input_;
    std::string line_;
    std::size_t line_number_ = 0;
    std::unordered_set<std::string> tables_;
};

/**
 * Utility function to parse a file as a TOML file. Returns the root table.
 * Throws a parse_exception if the file cannot be opened.
 */
inline table parse_file(const std::string& filename)
{
    std::ifstream file{filename};
    if (!file.is_open())
        throw parse_exception{filename + " could not be opened for parsing"};
    parser p{file};
    return p.parse();
}
}
#endif
