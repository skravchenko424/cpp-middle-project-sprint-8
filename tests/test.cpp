#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

namespace fs = std::filesystem;

namespace {

const std::string REFACTOR_TOOL_PATH =
    (fs::path(__FILE__).parent_path().parent_path() / "build" / "refactor_tool").string();

std::string readFile(const fs::path &path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void writeFile(const fs::path &path, const std::string &content) {
    std::ofstream out(path);
    out << content;
}

std::string runRefactor(const std::string &code) {
    auto tmp = fs::temp_directory_path() / "refactor_test.cpp";
    writeFile(tmp, code);
    std::string cmd = REFACTOR_TOOL_PATH + " " + tmp.string() + " -- -std=c++17 2>/dev/null";
    std::system(cmd.c_str());
    auto result = readFile(tmp);
    fs::remove(tmp);
    return result;
}

}  // namespace

TEST(VirtualDtor, AddsVirtualToDefaultDtorWithDerived) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    ~Base() = default;\n"
                        "};\n"
                        "class Derived : public Base {};\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("virtual ~Base() = default;"), std::string::npos);
}

TEST(VirtualDtor, AddsVirtualToNonVirtualDtor) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    ~Base() {}\n"
                        "};\n"
                        "class Derived : public Base {};\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("virtual ~Base() {}"), std::string::npos);
}

TEST(VirtualDtor, AddsVirtualThroughInheritanceChain) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    ~Base() {}\n"
                        "};\n"
                        "class Mid : public Base {};\n"
                        "class Leaf : public Mid {};\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("virtual ~Base()"), std::string::npos);
}

TEST(VirtualDtor, DoesNotDuplicateVirtual) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    ~Base() {}\n"
                        "};\n"
                        "class Derived : public Base {};\n";

    std::string result = runRefactor(input);
    EXPECT_EQ(result.find("virtual virtual"), std::string::npos);
    EXPECT_NE(result.find("virtual ~Base()"), std::string::npos);
}

TEST(VirtualDtor, AlreadyVirtualUnchanged) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual ~Base() {}\n"
                        "};\n"
                        "class Derived : public Base {};\n";

    std::string result = runRefactor(input);
    EXPECT_EQ(result.find("virtual virtual"), std::string::npos);
    EXPECT_NE(result.find("virtual ~Base()"), std::string::npos);
}

TEST(VirtualDtor, StandaloneClassUnchanged) {
    std::string input = "class Standalone {\n"
                        "public:\n"
                        "    ~Standalone() {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_EQ(result.find("virtual"), std::string::npos);
}

TEST(Override, AddsOverrideToOverriddenMethod) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual void func() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void func() {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("void func() override"), std::string::npos);
}

TEST(Override, AddsOverrideWithParameters) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual void func(int a) {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void func(int a) {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("void func(int a) override"), std::string::npos);
}

TEST(Override, AddsOverrideForPureVirtual) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual void run() = 0;\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void run() {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("void run() override"), std::string::npos);
}

TEST(Override, AddsOverrideThroughIntermediateClass) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual void func() {}\n"
                        "};\n"
                        "class Mid : public Base {\n"
                        "public:\n"
                        "    void func() override {}\n"
                        "};\n"
                        "class Leaf : public Mid {\n"
                        "public:\n"
                        "    void func() {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    auto leaf_pos = result.find("class Leaf");
    ASSERT_NE(leaf_pos, std::string::npos);
    auto override_after_leaf = result.find("override", leaf_pos);
    EXPECT_NE(override_after_leaf, std::string::npos);
}

TEST(Override, MultipleMethodsInDerived) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual void a() {}\n"
                        "    virtual void b() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void a() {}\n"
                        "    void b() {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("void a() override"), std::string::npos);
    EXPECT_NE(result.find("void b() override"), std::string::npos);
}

TEST(Override, DestructorDoesNotGetOverride) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual ~Base() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    ~Derived() {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_EQ(result.find("~Derived() override"), std::string::npos);
}

TEST(Override, AlreadyHasOverrideUnchanged) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual void func() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void func() override {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_EQ(result.find("override override"), std::string::npos);
    EXPECT_NE(result.find("void func() override"), std::string::npos);
}

TEST(Override, NonVirtualBaseMethodUnchanged) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    void func() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void func() {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_EQ(result.find("override"), std::string::npos);
}

TEST(Override, UnrelatedMethodUnchanged) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual void func() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void other() {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_EQ(result.find("override"), std::string::npos);
}

TEST(RangeFor, AddsRefToConstAuto) {
    std::string input = "#include <vector>\n"
                        "#include <string>\n"
                        "struct T { int id; std::string name; };\n"
                        "void f() {\n"
                        "    std::vector<T> v;\n"
                        "    for (const auto x : v) {\n"
                        "        (void)x;\n"
                        "    }\n"
                        "}\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("for (const auto& x : v)"), std::string::npos);
    EXPECT_EQ(result.find("for (const auto x : v)"), std::string::npos);
}

TEST(RangeFor, AddsRefToExplicitType) {
    std::string input = "#include <vector>\n"
                        "#include <string>\n"
                        "struct T { int id; std::string name; };\n"
                        "void f() {\n"
                        "    std::vector<T> v;\n"
                        "    for (const T x : v) {\n"
                        "        (void)x;\n"
                        "    }\n"
                        "}\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("for (const T& x : v)"), std::string::npos);
    EXPECT_EQ(result.find("for (const T x : v)"), std::string::npos);
}

TEST(RangeFor, AddsRefToDecltype) {
    std::string input = "#include <vector>\n"
                        "#include <string>\n"
                        "struct T { int id; std::string name; };\n"
                        "void f() {\n"
                        "    std::vector<T> v;\n"
                        "    for (const decltype(v)::value_type x : v) {\n"
                        "        (void)x;\n"
                        "    }\n"
                        "}\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("for (const decltype(v)::value_type& x : v)"), std::string::npos);
}

TEST(RangeFor, FundamentalTypeUnchanged) {
    std::string input = "#include <vector>\n"
                        "void f() {\n"
                        "    std::vector<int> v;\n"
                        "    for (const int x : v) {\n"
                        "        (void)x;\n"
                        "    }\n"
                        "}\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("for (const int x : v)"), std::string::npos);
    EXPECT_EQ(result.find("for (const int& x : v)"), std::string::npos);
}

TEST(RangeFor, AlreadyRefUnchanged) {
    std::string input = "#include <vector>\n"
                        "#include <string>\n"
                        "struct T { int id; std::string name; };\n"
                        "void f() {\n"
                        "    std::vector<T> v;\n"
                        "    for (const auto& x : v) {\n"
                        "        (void)x;\n"
                        "    }\n"
                        "}\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("for (const auto& x : v)"), std::string::npos);
    EXPECT_EQ(result.find("for (const auto&& x : v)"), std::string::npos);
}