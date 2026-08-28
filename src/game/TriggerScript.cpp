#include "game/TriggerScript.h"

#include <cctype>
#include <cstdlib>

namespace bb {
namespace {

// A name is letters, digits, colons and underscores.
bool NameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == ':' || c == '_';
}

void SkipSpace(const std::string& s, std::size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

bool ParseCall(const std::string& s, std::size_t& i, ScriptCall& out);

// One argument. Three shapes turn up: a quoted literal wrapped in a type
// wrapper, a wrapper around a nested call, and (rarely) a bare name.
bool ParseArg(const std::string& s, std::size_t& i, ScriptArg& out) {
    SkipSpace(s, i);
    const std::size_t start = i;
    while (i < s.size() && NameChar(s[i])) ++i;
    const std::string head = s.substr(start, i - start);
    SkipSpace(s, i);
    if (i >= s.size() || s[i] != '(') {
        out.Text = head;
        return !head.empty();
    }
    ++i;  // past '('
    SkipSpace(s, i);

    // `variable(90)` wraps a bare number, not a typed literal: it is a handle
    // on one of the editor's objects, and which kind of object depends on the
    // slot it is passed in. See ScriptArg.
    if (head == "variable") {
        out.Kind = head;
        const std::size_t a = i;
        while (i < s.size() && s[i] != ')') ++i;
        out.Text = s.substr(a, i - a);
        if (i < s.size()) ++i;   // past ')'
        return true;
    }

    // `preset(` / `const(` / `function(` wrap one more thing; anything else
    // with a paren after it is a call in its own right.
    if (head == "preset" || head == "const" || head == "function") {
        out.Kind = head;
        SkipSpace(s, i);
        const std::size_t inner = i;
        while (i < s.size() && NameChar(s[i])) ++i;
        const std::string type = s.substr(inner, i - inner);
        SkipSpace(s, i);
        if (i < s.size() && s[i] == '(') {
            ++i;
            SkipSpace(s, i);
            if (i < s.size() && s[i] == '"') {
                // A typed literal: Type("text").
                ++i;
                const std::size_t a = i;
                while (i < s.size() && s[i] != '"') ++i;
                out.Type = type;
                out.Text = s.substr(a, i - a);
                if (i < s.size()) ++i;   // past the closing quote
                SkipSpace(s, i);
                if (i < s.size() && s[i] == ')') ++i;   // close Type(
            } else {
                // A nested call: function(Namespace::Name(...)).
                auto call = std::make_shared<ScriptCall>();
                call->Name = type;
                // ParseCall expects to be positioned just past the name's '('.
                for (;;) {
                    SkipSpace(s, i);
                    if (i >= s.size() || s[i] == ')') break;
                    ScriptArg a;
                    if (!ParseArg(s, i, a)) break;
                    call->Args.push_back(std::move(a));
                    SkipSpace(s, i);
                    if (i < s.size() && s[i] == ',') ++i;
                }
                if (i < s.size() && s[i] == ')') ++i;
                out.Call = std::move(call);
            }
        }
        SkipSpace(s, i);
        if (i < s.size() && s[i] == ')') ++i;   // close the wrapper
        return true;
    }

    // A call used directly as an argument.
    auto call = std::make_shared<ScriptCall>();
    call->Name = head;
    for (;;) {
        SkipSpace(s, i);
        if (i >= s.size() || s[i] == ')') break;
        ScriptArg a;
        if (!ParseArg(s, i, a)) break;
        call->Args.push_back(std::move(a));
        SkipSpace(s, i);
        if (i < s.size() && s[i] == ',') ++i;
    }
    if (i < s.size() && s[i] == ')') ++i;
    out.Kind = "function";
    out.Call = std::move(call);
    return true;
}

bool ParseCall(const std::string& s, std::size_t& i, ScriptCall& out) {
    SkipSpace(s, i);
    const std::size_t start = i;
    while (i < s.size() && NameChar(s[i])) ++i;
    out.Name = s.substr(start, i - start);
    if (out.Name.empty()) return false;
    SkipSpace(s, i);
    if (i >= s.size() || s[i] != '(') return true;   // a call with no arguments
    ++i;
    for (;;) {
        SkipSpace(s, i);
        if (i >= s.size() || s[i] == ')') break;
        ScriptArg a;
        if (!ParseArg(s, i, a)) break;
        out.Args.push_back(std::move(a));
        SkipSpace(s, i);
        if (i < s.size() && s[i] == ',') ++i;
    }
    if (i < s.size() && s[i] == ')') ++i;
    return true;
}

}  // namespace

int ScriptArg::Number() const { return std::atoi(Text.c_str()); }

PlayerRef PlayerRef::Parse(const std::string& text) {
    PlayerRef r;
    // The trailing digit, where there is one.
    int n = 0;
    const std::size_t at = text.find_last_of("0123456789");
    if (at != std::string::npos) {
        std::size_t from = at;
        while (from > 0 &&
               std::isdigit(static_cast<unsigned char>(text[from - 1])))
            --from;
        n = std::atoi(text.c_str() + from);
    }
    r.Index = n;
    if (text.compare(0, 15, "Computer player") == 0)
        r.Kind = n ? kComputer : kAnyComputer;
    else if (text.compare(0, 12, "Human player") == 0)
        r.Kind = n ? kHuman : kAnyHuman;
    else if (text.compare(0, 14, "Current player") == 0)
        r.Kind = kCurrent;
    else if (text.compare(0, 6, "Player") == 0 && n)
        r.Kind = kAbsolute;
    else
        r.Kind = kAny;
    return r;
}

std::string ScriptCall::Verb() const {
    const std::size_t at = Name.rfind("::");
    return at == std::string::npos ? Name : Name.substr(at + 2);
}

std::string ScriptCall::Space() const {
    const std::size_t at = Name.rfind("::");
    return at == std::string::npos ? std::string() : Name.substr(0, at);
}

bool ParseScriptCall(const std::string& text, ScriptCall& out) {
    out = ScriptCall{};
    std::size_t i = 0;
    return ParseCall(text, i, out) && !out.Name.empty();
}

}  // namespace bb
