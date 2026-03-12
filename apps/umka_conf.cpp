module;
#include <umka_api.h>
export module umka_conf;
import std;
export namespace umka_conf
{
struct umka_strarr
{
        const UmkaType *type;
        int64_t itemSize;
        const char **data;
};
struct umka_target
{
        const char *name;
        umka_strarr srcs;
        const char *type;
        umka_strarr deps;
};
class umka_target_cxx
{
    public:
        std::string name;
        std::vector<std::string> srcs;
        std::string type;
        std::vector<std::string> deps;
        umka_target_cxx(const umka_target &t) : name(t.name), type(t.type)
        {
            int src_len{umkaGetDynArrayLen(&t.srcs)};
            int dep_len{umkaGetDynArrayLen(&t.deps)};
            srcs.reserve(src_len);
            deps.reserve(dep_len);
            for (int j = 0; j < src_len; j++)
            {
                srcs.push_back(t.srcs.data[j]);
            }
            for (int j = 0; j < dep_len; j++)
            {
                deps.push_back(t.deps.data[j]);
            }
        }
        umka_target_cxx(const umka_target_cxx &) = default;
        umka_target_cxx(umka_target_cxx &&) = default;
        umka_target_cxx &operator=(const umka_target_cxx &) = default;
        umka_target_cxx &operator=(umka_target_cxx &&) = default;
        ~umka_target_cxx() = default;
};
struct umka_result
{
    public:
        const UmkaType *type;
        int64_t itemSize;
        umka_target *data;
};
class umka_conf
{
    public:
        const char *script;
        const char *func;
        Umka *umka{nullptr};
        umka_conf(const char *script_, const char *func_) : script{script_}, func{func_}
        {
            umka = umkaAlloc();
            umkaInit(umka, script, nullptr, 4096, nullptr, 0, nullptr, true, true, nullptr);
            umkaCompile(umka);
        }
        umka_conf(const umka_conf &) = delete;
        umka_conf &operator=(const umka_conf &) = delete;
        umka_conf(umka_conf &&other) = default;
        umka_conf &operator=(umka_conf &&other) = default;
        auto run() -> std::vector<umka_target_cxx>
        {
            umkaRun(umka);
            UmkaFuncContext umka_fn = {0};
            umkaGetFunc(umka, nullptr, func, &umka_fn);
            umka_result result_storage{};
            UmkaStackSlot result_slot = {};
            result_slot.ptrVal = &result_storage;
            umka_fn.result = &result_slot;
            umkaCall(umka, &umka_fn);
            auto *header = (umka_result *)(result_slot.ptrVal);
            int len = umkaGetDynArrayLen(header);
            std::vector<umka_target_cxx> targets;
            targets.reserve(len);
            for (int i = 0; i < len; i++)
                targets.emplace_back(header->data[i]);
            for (int i = 0; i < len; i++)
            {
                umkaDecRef(umka, header->data[i].srcs.data);
                umkaDecRef(umka, header->data[i].deps.data);
            }
            umkaDecRef(umka, header->data);
            umkaFree(umka);
            return targets;
        }
};
}
