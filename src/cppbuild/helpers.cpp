module;
export module cppbuild:helpers;
import std;
export namespace cppbuild
{
    auto calc_output_path(const std::filesystem::path &src,
                          const std::filesystem::path &src_root,
                          const std::filesystem::path &build_dir) -> std::filesystem::path
    {
        auto src_str = src.string();
        auto build_str = build_dir.string();
        auto root_str = src_root.string();

        if (src_str.starts_with(build_str))
        {
            return build_dir / src.lexically_relative(build_dir);
        }
        if (src_str.starts_with(root_str))
        {
            return build_dir / src.lexically_relative(src_root);
        }

        // Source is neither under build_dir nor src_root. Fall back to filename only.
        return build_dir / src.filename();
    }
} // namespace cppbuild
