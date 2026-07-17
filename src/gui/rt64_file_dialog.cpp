//
// RT64
//

#include "rt64_file_dialog.h"

#include <cassert>

#if !defined(__ANDROID__)
#include <nfd.h>
#endif

namespace RT64 {
    // FileDialog

    std::atomic<bool> FileDialog::isOpen = false;

#if defined(__ANDROID__)
    // On Android there is no native file-browser dialog. ROM/file selection is
    // handled by the Java Storage Access Framework launcher, which hands the
    // chosen path to the native side directly. These entry points are therefore
    // no-ops that return an empty path.
    void FileDialog::initialize() {}
    void FileDialog::finish() {}

    std::filesystem::path FileDialog::getDirectoryPath() {
        return {};
    }

    std::filesystem::path FileDialog::getOpenFilename(const std::vector<FileFilter> &filters) {
        (void)filters;
        return {};
    }

    std::filesystem::path FileDialog::getSaveFilename(const std::vector<FileFilter> &filters) {
        (void)filters;
        return {};
    }
#else
    void FileDialog::initialize() {
        NFD_Init();
    }

    void FileDialog::finish() {
        NFD_Quit();
    }

    static std::vector<nfdnfilteritem_t> convertFilters(const std::vector<FileFilter> &filters) {
        std::vector<nfdnfilteritem_t> nfdFilters;
        for (const FileFilter &filter : filters) {
            nfdFilters.emplace_back(nfdnfilteritem_t{ filter.description.c_str(), filter.extensions.c_str() });
        }

        return nfdFilters;
    }

    std::filesystem::path FileDialog::getDirectoryPath() {
        isOpen = true;

        std::filesystem::path path;
        nfdnchar_t *nfdPath = nullptr;
        nfdresult_t res = NFD_PickFolderN(&nfdPath, nullptr);
        if (res == NFD_OKAY) {
            path = std::filesystem::path(nfdPath);
            NFD_FreePathN(nfdPath);
        }

        isOpen = false;
        return path;
    }

    std::filesystem::path FileDialog::getOpenFilename(const std::vector<FileFilter> &filters) {
        isOpen = true;
        
        std::filesystem::path path;
        nfdnchar_t *nfdPath = nullptr;
        std::vector<nfdnfilteritem_t> nfdFilters = convertFilters(filters);
        nfdresult_t res = NFD_OpenDialogN(&nfdPath, nfdFilters.data(), nfdFilters.size(), nullptr);
        if (res == NFD_OKAY) {
            path = std::filesystem::path(nfdPath);
            NFD_FreePathN(nfdPath);
        }

        isOpen = false;
        return path;
    }

    std::filesystem::path FileDialog::getSaveFilename(const std::vector<FileFilter> &filters) {
        isOpen = true;

        std::filesystem::path path;
        nfdnchar_t *nfdPath = nullptr;
        std::vector<nfdnfilteritem_t> nfdFilters = convertFilters(filters);
        nfdresult_t res = NFD_SaveDialogN(&nfdPath, nfdFilters.data(), nfdFilters.size(), nullptr, nullptr);
        if (res == NFD_OKAY) {
            path = std::filesystem::path(nfdPath);
            NFD_FreePathN(nfdPath);
        }

        isOpen = false;
        return path;
    }
#endif
};