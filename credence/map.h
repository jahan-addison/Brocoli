/*****************************************************************************
 * Copyright (c) Jahan Addison
 *
 * This software is dual-licensed under the Apache License, Version 2.0 or
 * the GNU General Public License, Version 3.0 or later.
 *
 * You may use this work, in part or in whole, under the terms of either
 * license.
 *
 * See the LICENSE.Apache-v2 and LICENSE.GPL-v3 files in the project root
 * for the full text of these licenses.
 ****************************************************************************/

#pragma once

#include <algorithm>  // for lower_bound
#include <concepts>   // for constructible_from, convertible_to
#include <cstddef>    // for size_t, ptrdiff_t
#include <functional> // for less
#include <stdexcept>  // for out_of_range
#include <utility>    // for forward, pair
#include <vector>     // for vector

/****************************************************************************
 * Ordered Map
 *
 * A map that iterates in insertion order, unlike std::map, which orders by
 * key. The translation phases use it where declaration order is output
 * order, such as the globals of the data section.
 *
 *  Example:
 *
 *    first 10;
 *    second 20;
 *    third 30;
 *
 *  Becomes, in the data section:
 *
 *    first:
 *        .long 10
 *    second:
 *        .long 20
 *    third:
 *        .long 30
 *
 ****************************************************************************/

namespace credence {

/**
 * @brief Type comparable with a map's keys in both directions
 *
 * Examples:
 *
 *   Comparable_Key<std::string_view, std::string, std::less<>> // true
 *   Comparable_Key<char const*, std::string, std::less<>>      // true
 *   Comparable_Key<int, std::string, std::less<>>              // false
 */
template<typename K, typename Key, typename Compare>
concept Comparable_Key =
    requires(Compare const& compare, K const& key, Key const& stored) {
        { compare(key, stored) } -> std::convertible_to<bool>;
        { compare(stored, key) } -> std::convertible_to<bool>;
    };

/**
 * @brief A map of entries in insertion order, with a sorted index of positions
 *
 * Each key is stored once, in its entry. The index is the positions of the
 * entries sorted by key, so a lookup is a binary search, and with the
 * transparent std::less<> a std::string key is found by a std::string_view
 * or a string literal with no allocation.
 */
template<typename Key, typename Value, typename Compare = std::less<>>
class Ordered_Map
{
  public:
    using Entry = std::pair<Key, Value>;
    using Entries = std::vector<Entry>;
    using iterator = typename Entries::iterator;
    using const_iterator = typename Entries::const_iterator;
    using reverse_iterator = typename Entries::reverse_iterator;
    using const_reverse_iterator = typename Entries::const_reverse_iterator;

  private:
    using Index = std::vector<std::size_t>;

  public:
    constexpr Ordered_Map() = default;

  public:
    /**
     * @brief Check if two maps have equal entries in the same order, where the
     * index follows from the entries and isn't compared
     */
    [[nodiscard]] friend bool operator==(Ordered_Map const& a,
        Ordered_Map const& b)
    {
        return a.entries_ == b.entries_;
    }

  public:
    /**
     * @brief Insert an entry at the end, or set the value of an existing key
     *
     * An existing key keeps its position in insertion order.
     */
    template<Comparable_Key<Key, Compare> K = Key, typename V = Value>
    requires std::constructible_from<Key, K&&> and
             std::constructible_from<Value, V&&>
    constexpr void insert(K&& key, V&& value)
    {
        auto position = lower_bound(key);
        if (is_match(position, key)) {
            entries_[*position].second = std::forward<V>(value);
            return;
        }
        emplace_at(position, std::forward<K>(key), std::forward<V>(value));
    }

    /**
     * @brief Get a key's value, inserting a default at the end when the key is
     * new
     */
    template<Comparable_Key<Key, Compare> K = Key>
    requires std::constructible_from<Key, K&&>
    constexpr Value& operator[](K&& key)
    {
        auto position = lower_bound(key);
        if (is_match(position, key))
            return entries_[*position].second;
        return emplace_at(position, std::forward<K>(key), Value{}).second;
    }

    /**
     * @brief Get an existing key's value
     *
     * Throws std::out_of_range when the key does not exist, like std::map::at.
     */
    template<Comparable_Key<Key, Compare> K = Key>
    [[nodiscard]] constexpr Value& at(K const& key)
    {
        return entries_[index_of(key)].second;
    }

    template<Comparable_Key<Key, Compare> K = Key>
    [[nodiscard]] constexpr Value const& at(K const& key) const
    {
        return entries_[index_of(key)].second;
    }

    /**
     * @brief Find a key's entry, or end() when the key does not exist
     */
    template<Comparable_Key<Key, Compare> K = Key>
    [[nodiscard]] constexpr iterator find(K const& key)
    {
        auto position = lower_bound(key);
        return is_match(position, key) ? entry_at(*position) : entries_.end();
    }

    template<Comparable_Key<Key, Compare> K = Key>
    [[nodiscard]] constexpr const_iterator find(K const& key) const
    {
        auto position = lower_bound(key);
        return is_match(position, key) ? entry_at(*position) : entries_.end();
    }

    template<Comparable_Key<Key, Compare> K = Key>
    [[nodiscard]] constexpr bool contains(K const& key) const
    {
        return is_match(lower_bound(key), key);
    }

  public:
    /**
     * @brief Get the first, last, and second to last entries in insertion order
     *
     * The map must not be empty. prev() of a map with one entry is that
     * entry. Like an iterator, the reference is invalid after an insertion.
     */
    [[nodiscard]] constexpr Entry& first() { return entries_.front(); }
    [[nodiscard]] constexpr Entry const& first() const
    {
        return entries_.front();
    }
    [[nodiscard]] constexpr Entry& last() { return entries_.back(); }
    [[nodiscard]] constexpr Entry const& last() const
    {
        return entries_.back();
    }
    [[nodiscard]] constexpr Entry& prev()
    {
        return entries_.size() > 1 ? entries_[entries_.size() - 2]
                                   : entries_.back();
    }
    [[nodiscard]] constexpr Entry const& prev() const
    {
        return entries_.size() > 1 ? entries_[entries_.size() - 2]
                                   : entries_.back();
    }

  public:
    [[nodiscard]] constexpr std::size_t size() const noexcept
    {
        return entries_.size();
    }
    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return entries_.empty();
    }
    constexpr void clear() noexcept
    {
        entries_.clear();
        index_.clear();
    }
    constexpr void reserve(std::size_t capacity)
    {
        entries_.reserve(capacity);
        index_.reserve(capacity);
    }

  public:
    [[nodiscard]] constexpr iterator begin() noexcept
    {
        return entries_.begin();
    }
    [[nodiscard]] constexpr iterator end() noexcept { return entries_.end(); }
    [[nodiscard]] constexpr const_iterator begin() const noexcept
    {
        return entries_.begin();
    }
    [[nodiscard]] constexpr const_iterator end() const noexcept
    {
        return entries_.end();
    }
    [[nodiscard]] constexpr reverse_iterator rbegin() noexcept
    {
        return entries_.rbegin();
    }
    [[nodiscard]] constexpr reverse_iterator rend() noexcept
    {
        return entries_.rend();
    }
    [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept
    {
        return entries_.rbegin();
    }
    [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept
    {
        return entries_.rend();
    }

  private:
    /**
     * @brief Get the first index position whose key is not less than key
     */
    template<typename K>
    constexpr typename Index::const_iterator lower_bound(K const& key) const
    {
        return std::lower_bound(index_.begin(),
            index_.end(),
            key,
            [this](std::size_t index, K const& search) {
                return Compare{}(entries_[index].first, search);
            });
    }

    template<typename K>
    constexpr bool is_match(typename Index::const_iterator position,
        K const& key) const
    {
        return position != index_.end() and
               not Compare{}(key, entries_[*position].first);
    }

    template<typename K>
    constexpr std::size_t index_of(K const& key) const
    {
        auto position = lower_bound(key);
        if (not is_match(position, key))
            throw std::out_of_range("Ordered_Map::at: the key does not exist");
        return *position;
    }

    constexpr iterator entry_at(std::size_t index)
    {
        return entries_.begin() + static_cast<std::ptrdiff_t>(index);
    }

    constexpr const_iterator entry_at(std::size_t index) const
    {
        return entries_.begin() + static_cast<std::ptrdiff_t>(index);
    }

    /**
     * @brief Append an entry, and insert its position in the index
     *
     * The entry is removed when the index insertion throws, so the map is
     * unchanged on an exception.
     */
    template<typename K, typename V>
    constexpr Entry& emplace_at(typename Index::const_iterator position,
        K&& key,
        V&& value)
    {
        auto offset = position - index_.begin();
        auto& entry =
            entries_.emplace_back(std::forward<K>(key), std::forward<V>(value));
        try {
            index_.insert(index_.begin() + offset, entries_.size() - 1);
        } catch (...) {
            entries_.pop_back();
            throw;
        }
        return entry;
    }

  private:
    Entries entries_{};
    Index index_{};
};

} // namespace credence
