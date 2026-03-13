module;
#include <umka_api.h>
#include <fmt/base.h>
#include <fmt/os.h>
#include <fmt/ranges.h>
export module umka_cxx;
import std;
export namespace umka_cxx
{
struct cstr_arr
{
    public:
        const UmkaType *type;
        int64_t itemSize;
        const char **data;
};

struct ctarget
{
    public:
        const char *name;
        const char *type;
        cstr_arr srcs;
        cstr_arr deps;
        cstr_arr cxxflags;
        cstr_arr cflags;
        cstr_arr ldflags;
};

struct cresult
{
    public:
        const UmkaType *type;
        int64_t itemSize;
        ctarget *data;
};

class cxxtarget
{
    public:
        std::string name{};
        std::vector<std::string> srcs{};
        std::string type{};
        std::vector<std::string> deps{};
        std::vector<std::string> cxxflags{};
        std::vector<std::string> cflags{};
        std::vector<std::string> ldflags{};
        cxxtarget() = default;
        cxxtarget(cxxtarget &&) = default;
        cxxtarget &operator=(cxxtarget &&) = default;
        cxxtarget(ctarget *target) : name(target->name), type(target->type)
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
            {
                srcs.push_back(target->srcs.data[j]);
            }
            for (int j = 0; j < dep_len; j++)
            {
                deps.push_back(target->deps.data[j]);
            }
            for (int j = 0; j < cxxflags_len; j++)
            {
                cxxflags.push_back(target->cxxflags.data[j]);
            }
            for (int j = 0; j < cflags_len; j++)
            {
                cflags.push_back(target->cflags.data[j]);
            }
            for (int j = 0; j < ldflags_len; j++)
            {
                ldflags.push_back(target->ldflags.data[j]);
            }
        }
};

class umka
{
    public:
        Umka *umka_c{nullptr};
        umka() : umka_c{umkaAlloc()} {};
        auto run(const std::filesystem::path &script, const char *func_name) -> std::vector<cxxtarget>
        {
            umkaInit(umka_c, script.c_str(), nullptr, 4096, nullptr, 0, nullptr, true, true, nullptr);

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
            UmkaFuncContext umka_fn{0};
            umkaGetFunc(umka_c, nullptr, func_name, &umka_fn);
            cresult result_storage{};
            UmkaStackSlot result_slot{};
            result_slot.ptrVal = &result_storage;
            umka_fn.result = &result_slot;
            umkaCall(umka_c, &umka_fn);

            auto *header = (cresult *)(result_slot.ptrVal);
            int len = umkaGetDynArrayLen(header);
            std::vector<cxxtarget> targets;
            targets.reserve(len);
            for (int i = 0; i < len; i++)
            {
                targets.emplace_back(&header->data[i]);
            }
            for (int i = 0; i < len; i++)
            {
                umkaDecRef(umka_c, header->data[i].srcs.data);
                umkaDecRef(umka_c, header->data[i].deps.data);
                umkaDecRef(umka_c, header->data[i].cxxflags.data);
                umkaDecRef(umka_c, header->data[i].cflags.data);
                umkaDecRef(umka_c, header->data[i].ldflags.data);
            }
            umkaDecRef(umka_c, header->data);
            umkaFree(umka_c);
            return targets;
        }
};
} // namespace umka_cxx
