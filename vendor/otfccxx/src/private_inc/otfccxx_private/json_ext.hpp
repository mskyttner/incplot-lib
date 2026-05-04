#pragma once

#include <otfccxx/otfccxx.hpp>
#include <otfccxx_private/machinery_uptr.hpp>


namespace otfccxx {
using namespace std::literals;

namespace json_ext {

[[maybe_unused]] static inline struct _json_value *
get(json_value const &rf, std::string_view key) {
    if (rf.type == json_object) {
        for (unsigned int i = 0; i < rf.u.object.length; ++i) {
            if (std::string_view(rf.u.object.values[i].name, rf.u.object.values[i].name_length) == key) {
                return rf.u.object.values[i].value;
            }
        }
    }
    return nullptr;
}

[[maybe_unused]] static inline struct _json_value *
get(json_value const &rf, const char *key) {
    if (rf.type == json_object) {
        for (unsigned int i = 0; i < rf.u.object.length; ++i) {
            if (! strcmp(rf.u.object.values[i].name, key)) { return rf.u.object.values[i].value; }
        }
    }
    return nullptr;
}

[[maybe_unused]] static inline struct _json_value *
get(json_value const &rf, int index) {
    if (rf.type != json_array || index < 0 || ((unsigned int)index) >= rf.u.array.length) { return nullptr; }
    return rf.u.array.values[index];
}

// Helper to free a json_value and its children (simple recursive free)
static inline void
free_jsonValue(json_value *val) {
    if (! val) { return; }

    switch (val->type) {
        case json_object:
            {
                for (unsigned int i = 0; i < val->u.object.length; ++i) {
                    json_object_entry &entry = val->u.object.values[i];

                    // Free key name (heap allocated by otfcc)
                    std::free(entry.name);
                    // Free value subtree
                    free_jsonValue(entry.value);
                }

                std::free(val->u.object.values);
                break;
            }
        case json_array:
            {
                for (unsigned int i = 0; i < val->u.array.length; ++i) { free_jsonValue(val->u.array.values[i]); }
                std::free(val->u.array.values);
                break;
            }
        case json_string:
            // otfcc allocates strings
            std::free(val->u.string.ptr);
            break;

        case json_integer:
        case json_double:
        case json_boolean:
        case json_null:
        case json_none:
        default:           break;
    }

    std::free(val);
}

// Remove a named member from a JSON object.
// Returns true if something was removed.
[[maybe_unused]] static inline std::expected<bool, err_modifier>
remove_objectMemberByName(json_value *obj, std::string_view key) {
    if (! obj) { return false; }
    if (obj->type != json_object) { return std::unexpected(err_modifier::unexpectedJSONValueType); }

    size_t write_i = 0;
    bool   removed = false;

    for (size_t i = 0; i < obj->u.object.length; ++i) {
        auto &entry = obj->u.object.values[i];

        if (! removed && std::string_view(entry.name) == key) {
            // Free the value for the removed member
            free_jsonValue(entry.value);
            removed = true;
            continue; // skip adding to new slot
        }

        // compact in place
        if (write_i != i) { obj->u.object.values[write_i] = entry; }
        ++write_i;
    }

    if (! removed) { return false; }

    // Shrink array
    obj->u.object.length = write_i;
    obj->u.object.values =
        (decltype(obj->u.object.values))std::realloc(obj->u.object.values, write_i * sizeof(*obj->u.object.values));

    return true;
}

template <typename P>
requires std::predicate<P, const json_object_entry &>
[[maybe_unused]] static inline std::expected<size_t, err_modifier>
remove_objectMembers(json_value *obj, P const pred) {
    if (! obj) { return false; }
    if (obj->type != json_object) { return std::unexpected(err_modifier::unexpectedJSONValueType); }

    size_t write_i = 0;
    size_t removed = 0uz;

    for (size_t i = 0; i < obj->u.object.length; ++i) {
        auto &entry = obj->u.object.values[i];

        if (pred(entry)) {
            // Free the value for the removed member
            free_jsonValue(entry.value);
            removed++;
            continue; // skip adding to new slot
        }

        // compact in place
        if (write_i != i) { obj->u.object.values[write_i] = entry; }
        ++write_i;
    }

    // Shrink array if we did some removal. It is not an error to remove nothing
    if (write_i != obj->u.object.length) {
        obj->u.object.length = write_i;
        obj->u.object.values =
            (decltype(obj->u.object.values))std::realloc(obj->u.object.values, write_i * sizeof(*obj->u.object.values));
    }

    return removed;
}

// Remove, but before removal execute EXTR_FN on the element being removed, return the result in a vector of return
// types of EXTR_FN. (note that the element is deleted, so the EXTR_FN must not return a reference to some of its
// content)
template <typename P, typename EXTR_FN>
requires std::predicate<P, const json_object_entry &> && std::invocable<EXTR_FN, const json_object_entry &> &&
         (! std::same_as<std::invoke_result_t<EXTR_FN, const json_object_entry &>, void>)
[[maybe_unused]] static inline auto
remove_objectMembers(json_value *obj, P const pred, EXTR_FN const extr_fn)
    -> std::expected<std::vector<std::invoke_result_t<EXTR_FN, const json_object_entry &>>, err_modifier> {

    if (! obj) { return false; }
    if (obj->type != json_object) { return std::unexpected(err_modifier::unexpectedJSONValueType); }

    size_t                                                                write_i = 0;
    std::vector<std::invoke_result_t<EXTR_FN, const json_object_entry &>> contentFromRemoved;

    for (size_t i = 0; i < obj->u.object.length; ++i) {
        auto &entry = obj->u.object.values[i];
        if (pred(entry)) {
            contentFromRemoved.push_back(EXTR_FN(entry));

            // Free the value for the removed member
            free_jsonValue(entry.value);
            continue; // skip adding to new slot
        }

        // compact in place
        if (write_i != i) { obj->u.object.values[write_i] = entry; }
        ++write_i;
    }

    // Shrink array if we did some removal. It is not an error to remove nothing
    if (write_i != obj->u.object.length) {
        obj->u.object.length = write_i;
        obj->u.object.values =
            (decltype(obj->u.object.values))std::realloc(obj->u.object.values, write_i * sizeof(*obj->u.object.values));
    }

    return contentFromRemoved;
}

// Get methods ... obtain some json_value* in the tree by some logic
[[maybe_unused]]
static inline std::expected<json_value *, err_modifier>
get_byNames(json_value *root, std::vector<std::string_view> const &toGet) {
    if (root == nullptr) { return std::unexpected(err_modifier::unexpectedNullptr); }
    json_value *cur = root;

    for (auto &oneSV : toGet) {
        cur = get(*cur, oneSV);
        if (cur == nullptr) { return std::unexpected(err_modifier::unexpectedNullptr); }
    }
    return cur;
}

[[maybe_unused]]
static inline std::expected<std::optional<json_value *>, err_modifier>
getMaybe_byNames(json_value *root, std::vector<std::string_view> const &toGet) {
    if (root == nullptr) { return std::unexpected(err_modifier::unexpectedNullptr); }
    json_value *cur = root;

    for (auto const &oneName : toGet) {
        cur = get(*cur, oneName);
        if (cur == nullptr) { return std::nullopt; }
    }
    return cur;
}


[[maybe_unused]]
static inline std::expected<std::vector<json_value *>, err_modifier>
get_byNamesInTree(json_value *root, std::vector<std::vector<std::string_view>> const &toGet) {
    if (root == nullptr) { return std::unexpected(err_modifier::unexpectedNullptr); }
    size_t                    curLvl = 0;
    std::vector<json_value *> res;

    auto const recSolver = [&](this auto const &self, json_value *cur) -> std::optional<err_modifier> {
        if (curLvl == toGet.size()) {
            res.push_back(cur);
            return std::nullopt;
        }
        for (auto const &oneName : toGet.at(curLvl)) {
            auto newJV = get(*cur, oneName);
            if (newJV == nullptr) { return err_modifier::missingJSONKey; }

            curLvl++;
            if (auto res = self(newJV); res.has_value()) { return res; }
            curLvl--;
        }
        return std::nullopt;
    };

    if (auto res = recSolver(root); res.has_value()) { return std::unexpected(res.value()); }
    return res;
}

static inline std::expected<std::vector<std::optional<json_value *>>, err_modifier>
getMaybe_byNamesInTree(json_value *root, std::vector<std::vector<std::string_view>> const &toGet) {
    if (root == nullptr) { return std::unexpected(err_modifier::unexpectedNullptr); }
    size_t                                   curLvl = 0;
    std::vector<std::optional<json_value *>> res;

    auto const recSolver = [&](this auto const &self, json_value *cur) -> void {
        if (curLvl == toGet.size()) {
            res.push_back(cur);
            return;
        }
        for (auto const &oneName : toGet.at(curLvl)) {
            auto newJV = get(*cur, oneName);
            if (newJV == nullptr) {
                size_t curLvlCpy      = curLvl;
                size_t emptyOptsCount = 1;
                while (++curLvlCpy < toGet.size()) { emptyOptsCount *= toGet.at(curLvlCpy).size(); }
                while (emptyOptsCount-- > 0) { res.push_back(std::nullopt); }
                continue;
            }

            curLvl++;
            self(newJV);
            curLvl--;
        }
    };

    recSolver(root);
    return res;
}


} // namespace json_ext
} // namespace otfccxx