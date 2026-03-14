module;
#include <umka_api.h>
#include <fmt/base.h>
#include <fmt/os.h>
#include <fmt/ranges.h>
export module cppbuild.umka;
import std;
export namespace cppbuild::umka
{
    constexpr char cppbuild_mod[] = {
#embed "umka_modules/cppbuild.um"
        , 0};

    struct umka_str_arr
    {
        public:
            const UmkaType *type;
            int64_t itemSize;
            const char **data;
    };

    struct umka_build_target
    {
        public:
            const char *name;
            const char *type;
            umka_str_arr srcs;
            umka_str_arr deps;
            umka_str_arr cxxflags;
            umka_str_arr cflags;
            umka_str_arr ldflags;
    };

    struct umka_result
    {
        public:
            const UmkaType *type;
            int64_t itemSize;
            umka_build_target *data;
    };

    class umka_cxx_build_target
    {
        public:
            std::string name{};
            std::vector<std::string> srcs{};
            std::string type{};
            std::vector<std::string> deps{};
            std::vector<std::string> cxxflags{};
            std::vector<std::string> cflags{};
            std::vector<std::string> ldflags{};
            umka_cxx_build_target() = default;
            umka_cxx_build_target(umka_cxx_build_target &&) = default;
            umka_cxx_build_target &operator=(umka_cxx_build_target &&) = default;
            umka_cxx_build_target(umka_build_target *target) : name(target->name), type(target->type)
            {
                int src_len{umkaGetDynArrayLen(&target->srcs)};
                int dep_len{umkaGetDynArrayLen(&target->deps)};
                int cxxflags_len{umkaGetDynArrayLen(&target->cxxflags)};
                int cflags_len{umkaGetDynArrayLen(&target->cflags)};
                int ldflags_len{umkaGetDynArrayLen(&target->ldflags)};
                srcs.reserve(src_len);
                deps.reserve(dep_len);
                cxxflags.reserve(cxxflags_len);
                cflags.reserve(cflags_len);
                ldflags.reserve(ldflags_len);
                for (int j = 0; j < src_len; j++)
                    srcs.push_back(target->srcs.data[j]);
                for (int j = 0; j < dep_len; j++)
                    deps.push_back(target->deps.data[j]);
                for (int j = 0; j < cxxflags_len; j++)
                    cxxflags.push_back(target->cxxflags.data[j]);
                for (int j = 0; j < cflags_len; j++)
                    cflags.push_back(target->cflags.data[j]);
                for (int j = 0; j < ldflags_len; j++)
                    ldflags.push_back(target->ldflags.data[j]);
            }
    };

    struct umka_cxx_result
    {
        public:
            std::vector<umka_cxx_build_target> build_targets;
            static auto from(umka_result *result, int len) -> umka_cxx_result
            {
                umka_cxx_result r;
                r.build_targets.reserve(len);
                for (int i = 0; i < len; i++)
                    r.build_targets.emplace_back(&result->data[i]);
                return r;
            }
    };

    class umka
    {
        public:
            Umka *umka_c{nullptr};
            umka() : umka_c{umkaAlloc()} {};
            auto run(const std::filesystem::path &script, const char *func_name) -> umka_cxx_result
            {
                umkaInit(umka_c, script.c_str(), nullptr, 4096, nullptr, 0, nullptr, true, true, nullptr);
                umkaAddModule(umka_c, "cppbuild.um", cppbuild_mod);
                if (!umkaCompile(umka_c))
                {
                    fmt::println("compile error: {}", umkaGetError(umka_c)->msg);
                    std::terminate();
                }
                if (umkaRun(umka_c) != 0)
                {
                    fmt::println("run error: {}", umkaGetError(umka_c)->msg);
                    std::terminate();
                }
                UmkaFuncContext umka_fn{};
                umkaGetFunc(umka_c, nullptr, func_name, &umka_fn);
                umka_result result_storage{};
                UmkaStackSlot result_slot{};
                result_slot.ptrVal = &result_storage;
                umka_fn.result = &result_slot;
                umkaCall(umka_c, &umka_fn);
                auto *result = (umka_result *)(result_slot.ptrVal);
                int len = umkaGetDynArrayLen(result);
                umka_cxx_result cxx_result{umka_cxx_result::from(result, len)};
                for (int i = 0; i < len; i++)
                {
                    umkaDecRef(umka_c, result->data[i].srcs.data);
                    umkaDecRef(umka_c, result->data[i].deps.data);
                    umkaDecRef(umka_c, result->data[i].cxxflags.data);
                    umkaDecRef(umka_c, result->data[i].cflags.data);
                    umkaDecRef(umka_c, result->data[i].ldflags.data);
                }
                umkaDecRef(umka_c, result->data);
                umkaFree(umka_c);
                return cxx_result;
            }
    };
} // namespace cppbuild::umka
