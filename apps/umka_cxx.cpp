module;
#include <umka_api.h>
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
        cstr_arr srcs;
        const char *type;
        cstr_arr deps;
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
        cxxtarget() = default;
        cxxtarget(cxxtarget &&) = default;
        cxxtarget &operator=(cxxtarget &&) = default;
        cxxtarget(ctarget *target) : name(target->name), type(target->type)
        {
            int src_len{umkaGetDynArrayLen(&target->srcs)};
            int dep_len{umkaGetDynArrayLen(&target->deps)};
            srcs.reserve(src_len);
            deps.reserve(dep_len);
            for (int j = 0; j < src_len; j++)
            {
                srcs.push_back(target->srcs.data[j]);
            }
            for (int j = 0; j < dep_len; j++)
            {
                deps.push_back(target->deps.data[j]);
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
            umkaCompile(umka_c);
            umkaRun(umka_c);

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
            }
            umkaDecRef(umka_c, header->data);
            umkaFree(umka_c);
            return targets;
        }
};
} // namespace umka_cxx
