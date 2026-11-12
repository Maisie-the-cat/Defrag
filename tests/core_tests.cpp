#include "../defrag_core.cpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace {
void write_file(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
    if (!out) throw std::runtime_error("failed to create test input");
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
}

int main() {
    char temp_template[] = "/tmp/defrag-test-XXXXXX";
    char* directory = mkdtemp(temp_template);
    assert(directory != nullptr);
    const fs::path dir(directory);

    DefragCore::GenericDefragmenter defragmenter;
    const std::string payload(16387, 'x');

    const fs::path ordinary = dir / "ordinary.bin";
    write_file(ordinary, payload);
    assert(chmod(ordinary.c_str(), 0640) == 0);
    assert(defragmenter.defragment(ordinary, nullptr));
    assert(read_file(ordinary) == payload);
    struct stat ordinary_stat{};
    assert(stat(ordinary.c_str(), &ordinary_stat) == 0);
    assert((ordinary_stat.st_mode & 0777) == 0640);

    const fs::path empty = dir / "empty";
    write_file(empty, "");
    assert(defragmenter.defragment(empty, nullptr));
    assert(read_file(empty).empty());

    const fs::path matching_suffix = dir / "already.defrag_tmp";
    write_file(matching_suffix, payload);
    assert(defragmenter.defragment(matching_suffix, nullptr));
    assert(read_file(matching_suffix) == payload);

    const fs::path linked = dir / "linked.bin";
    write_file(linked, payload);
    const fs::path alias = dir / "alias.bin";
    assert(link(linked.c_str(), alias.c_str()) == 0);
    assert(!defragmenter.defragment(linked, nullptr));
    assert(read_file(linked) == payload);
    assert(read_file(alias) == payload);

    const fs::path fake_bin = dir / "bin";
    fs::create_directory(fake_bin);
    const fs::path fake_fsr = fake_bin / "xfs_fsr";
    const fs::path marker = dir / "xfs-argv.txt";
    {
        std::ofstream script(fake_fsr);
        script << "#!/bin/sh\nprintf '%s' \"$1\" > \"$XFS_FSR_TEST_MARKER\"\n";
    }
    assert(chmod(fake_fsr.c_str(), 0700) == 0);
    const char* old_path = std::getenv("PATH");
    const std::string saved_path = old_path ? old_path : "";
    assert(setenv("PATH", (fake_bin.string() + ":" + saved_path).c_str(), 1) == 0);
    assert(setenv("XFS_FSR_TEST_MARKER", marker.c_str(), 1) == 0);
    const fs::path xfs_input = dir / "xfs file; literal.bin";
    write_file(xfs_input, payload);
    DefragCore::XfsDefragmenter xfs_defragmenter;
    assert(xfs_defragmenter.defragment(xfs_input, nullptr));
    assert(read_file(marker) == fs::canonical(xfs_input).string());

    {
        std::ofstream script(fake_fsr);
        script << "#!/bin/sh\nexit 7\n";
    }
    assert(chmod(fake_fsr.c_str(), 0700) == 0);
    assert(!xfs_defragmenter.defragment(xfs_input, nullptr));
    if (old_path) assert(setenv("PATH", saved_path.c_str(), 1) == 0);
    else assert(unsetenv("PATH") == 0);
    assert(unsetenv("XFS_FSR_TEST_MARKER") == 0);

    std::error_code ec;
    fs::remove_all(dir, ec);
    assert(!ec);
    std::cout << "Core regression tests passed\n";
}
