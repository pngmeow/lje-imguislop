// Build-time tool: produces a copy of the CEF runtime whose module names cannot
// collide with a host process that ships its own Chromium.
//
// Garry's Mod is the motivating case - html_chromium.dll statically imports
// libcef.dll, so CEF 86 (libcef.dll + chrome_elf.dll) is already in the process
// by the time we are injected. Windows resolves imports by module *base name*,
// so our libcef.dll binds to whichever chrome_elf.dll is loaded first no matter
// which directory each was loaded from. Against an older chrome_elf that means
// missing exports - GetProductInfo_ExportThunk is the first one to blow up.
// LOAD_WITH_ALTERED_SEARCH_PATH cannot help here: search order only applies to
// modules that are not already loaded.
//
// Renaming both files sidesteps the shared namespace. libcef.dll names its
// dependency in the import directory as a NUL terminated ASCII string, so the
// replacement chrome_elf name has to be exactly as long as the original for the
// patch to be an in-place overwrite.
//
//   cef-rename <cef_binary_dir> <out_dir> <cef_name> <elf_name>

#include <windows.h>

#include <cstdio>
#include <string>

namespace {

constexpr char kElfImportName[] = "chrome_elf.dll";

std::wstring join(const std::wstring &dir, const std::wstring &name) {
  return dir + L'\\' + name;
}

// True when dst exists and is no older than src, i.e. a previous build already
// produced it. Copying 285 MB on every build is otherwise unbearable.
bool up_to_date(const std::wstring &src, const std::wstring &dst) {
  WIN32_FILE_ATTRIBUTE_DATA source{};
  WIN32_FILE_ATTRIBUTE_DATA target{};
  if (!GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &source))
    return false;
  if (!GetFileAttributesExW(dst.c_str(), GetFileExInfoStandard, &target))
    return false;
  return CompareFileTime(&target.ftLastWriteTime, &source.ftLastWriteTime) >= 0;
}

bool copy(const std::wstring &src, const std::wstring &dst) {
  if (CopyFileW(src.c_str(), dst.c_str(), FALSE))
    return true;
  fwprintf(stderr, L"cef-rename: cannot copy %s to %s (error %lu)\n", src.c_str(), dst.c_str(),
           GetLastError());
  return false;
}

// Overwrites the sole occurrence of `from` with `to`. Both carry their
// terminating NUL so a match can only ever be a whole string, never a prefix of
// a longer one, and the terminator lands where it belongs.
bool patch_string(const std::wstring &file, const std::string &from, const std::string &to) {
  if (from.size() != to.size()) {
    fwprintf(stderr, L"cef-rename: replacement name must be %zu bytes\n", from.size() - 1);
    return false;
  }

  HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    fwprintf(stderr, L"cef-rename: cannot open %s (error %lu)\n", file.c_str(), GetLastError());
    return false;
  }

  LARGE_INTEGER size{};
  if (!GetFileSizeEx(handle, &size) || size.QuadPart <= 0) {
    fwprintf(stderr, L"cef-rename: cannot size %s (error %lu)\n", file.c_str(), GetLastError());
    CloseHandle(handle);
    return false;
  }

  HANDLE mapping = CreateFileMappingW(handle, nullptr, PAGE_READWRITE, 0, 0, nullptr);
  if (!mapping) {
    fwprintf(stderr, L"cef-rename: cannot map %s (error %lu)\n", file.c_str(), GetLastError());
    CloseHandle(handle);
    return false;
  }

  auto *base = static_cast<char *>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0));
  if (!base) {
    fwprintf(stderr, L"cef-rename: cannot view %s (error %lu)\n", file.c_str(), GetLastError());
    CloseHandle(mapping);
    CloseHandle(handle);
    return false;
  }

  const auto total = static_cast<size_t>(size.QuadPart);
  char *hit = nullptr;
  size_t matches = 0;
  for (size_t offset = 0; offset + from.size() <= total;) {
    auto *candidate = static_cast<char *>(
        memchr(base + offset, from.front(), total - offset - from.size() + 1));
    if (!candidate)
      break;
    if (memcmp(candidate, from.data(), from.size()) == 0) {
      hit = candidate;
      ++matches;
    }
    offset = static_cast<size_t>(candidate - base) + 1;
  }

  bool ok = false;
  if (matches != 1) {
    // Anything other than the single import directory entry means this CEF
    // build references the name somewhere else too (a runtime LoadLibrary, say)
    // and a blind rename would silently break it.
    fwprintf(stderr, L"cef-rename: expected exactly 1 occurrence of \"%hs\" in %s, found %zu\n",
             from.c_str(), file.c_str(), matches);
  } else {
    memcpy(hit, to.data(), to.size());
    ok = FlushViewOfFile(hit, to.size()) != 0;
    if (!ok)
      fwprintf(stderr, L"cef-rename: cannot flush %s (error %lu)\n", file.c_str(), GetLastError());
  }

  UnmapViewOfFile(base);
  CloseHandle(mapping);
  CloseHandle(handle);
  return ok;
}

std::string narrow(const std::wstring &value) {
  auto size = WideCharToMultiByte(CP_ACP, 0, value.data(), static_cast<int>(value.size()), nullptr,
                                  0, nullptr, nullptr);
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_ACP, 0, value.data(), static_cast<int>(value.size()), result.data(), size,
                      nullptr, nullptr);
  return result;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  if (argc != 5) {
    fwprintf(stderr, L"usage: cef-rename <cef_binary_dir> <out_dir> <cef_name> <elf_name>\n");
    return 1;
  }

  const std::wstring source_dir = argv[1];
  const std::wstring out_dir = argv[2];
  const std::wstring cef_name = argv[3];
  const std::wstring elf_name = argv[4];

  const auto cef_src = join(source_dir, L"libcef.dll");
  const auto cef_dst = join(out_dir, cef_name);
  const auto elf_src = join(source_dir, L"chrome_elf.dll");
  const auto elf_dst = join(out_dir, elf_name);

  if (!up_to_date(elf_src, elf_dst) && !copy(elf_src, elf_dst))
    return 1;

  if (!up_to_date(cef_src, cef_dst)) {
    if (!copy(cef_src, cef_dst))
      return 1;

    // The NUL is part of both strings; see patch_string.
    std::string from(kElfImportName, sizeof(kElfImportName));
    std::string to = narrow(elf_name);
    to.push_back('\0');

    if (!patch_string(cef_dst, from, to)) {
      // Leave no half-patched DLL behind for the next build to accept as
      // up to date.
      DeleteFileW(cef_dst.c_str());
      return 1;
    }

    wprintf(L"cef-rename: %s imports %s\n", cef_name.c_str(), elf_name.c_str());
  }

  return 0;
}
