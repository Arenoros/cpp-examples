#include <windows.h>
#include <boost/filesystem.hpp>

void ExampleChangeNotification() {
    fs::path dir = "D:/test";
    auto hndl = FindFirstChangeNotificationW(dir.wstring().c_str(),
                                             TRUE,
                                             FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                                 FILE_NOTIFY_CHANGE_SIZE);
    bool m_stop = false;
    printf("\nWaiting for notification...\n");
    while (!m_stop) {
        // Wait for notification.
        // auto stat = WaitForSingleObject(hndl, 10000);
        auto stat = WaitForMultipleObjects(1, &hndl, FALSE, 10000);

        switch (stat) {
        case WAIT_OBJECT_0:
            if (FindNextChangeNotification(hndl) == FALSE) {
                wprintf(L"\n ERROR: FindNextChangeNotification function failed.\n");
                ExitProcess(GetLastError());
            }
            break;

            // case WAIT_OBJECT_0 + 1:

            //    // A directory was created, renamed, or deleted.
            //    // Refresh the tree and restart the notification.

            //    RefreshTree(dir.wstring().c_str());
            //    if (FindNextChangeNotification(hndl) == FALSE) {
            //        wprintf(L"\n ERROR: FindNextChangeNotification function failed.\n");
            //        ExitProcess(GetLastError());
            //    }
            //    break;

        case WAIT_TIMEOUT:

            // A timeout occurred, this would happen if some value other
            // than INFINITE is used in the Wait call and no changes occur.
            // In a single-threaded environment you might not want an
            // INFINITE wait.

            printf("\nNo changes in the timeout period.\n");
            break;

        default:
            printf("\n ERROR: Unhandled dwWaitStatus.\n");
            ExitProcess(GetLastError());
            break;
        }
    }
    FindCloseChangeNotification(hndl);
}

void AsyncReadDirectory() {
    lib::fs::path p = "D:/test";
    bool m_stop = false;

    /* 
     * https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw
     * ReadDirectoryChangesW fails with ERROR_INVALID_PARAMETER when the buffer length is greater than 64 KB 
     * and the application is monitoring a directory over the network. 
     * This is due to a packet size limitation with the underlying file sharing protocols.
    */
    typedef struct _DIRECTORY_INFO {
        HANDLE hDir;
        TCHAR lpszDirName[MAX_PATH];
        CHAR lpBuffer[64*1024];
        DWORD dwBufLength;
        OVERLAPPED Overlapped;
    } DIRECTORY_INFO, *PDIRECTORY_INFO, *LPDIRECTORY_INFO;
    DIRECTORY_INFO di{};
    di.hDir = CreateFileW(p.wstring().c_str(), /* pointer to the file name */
                          FILE_LIST_DIRECTORY, /* (this is important to be FILE_LIST_DIRECTORY!) access
                                                  (read-write) mode */
                          FILE_SHARE_WRITE | FILE_SHARE_READ |
                              FILE_SHARE_DELETE, /* (file share write is needed, or else user is not able to
                                                    rename file while you hold it) share mode */
                          NULL,                  /* security descriptor */
                          OPEN_EXISTING,         /* how to create */
                          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, /* file attributes */
                          NULL                                               /* file with attributes to copy */
    );
    if (di.hDir == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(std::string("Could not open ").append(p.string()).append(" for watching!"));
    }
    HANDLE hCompPort = nullptr;
    hCompPort = CreateIoCompletionPort(di.hDir, hCompPort, (ULONG_PTR)&di, 0);
    DWORD cbOffset;
    DWORD notifyFilter = FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION |
                         FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_FILE_NAME;
    ReadDirectoryChangesW(di.hDir, di.lpBuffer, 4096, TRUE, notifyFilter, &di.dwBufLength, &di.Overlapped, NULL);
    while (!m_stop) {
        DWORD numBytes = 0;
        LPOVERLAPPED lpOverlapped = 0;
        LPDIRECTORY_INFO ptr_di{};
        auto res = GetQueuedCompletionStatus(hCompPort, &numBytes, (PULONG_PTR)&ptr_di, &lpOverlapped, INFINITE);
        if (ptr_di) {
            auto fni = (PFILE_NOTIFY_INFORMATION)ptr_di->lpBuffer;

            do {
                cbOffset = fni->NextEntryOffset;
                std::wstring path(fni->FileName, fni->FileNameLength);
                if (fni->Action == FILE_ACTION_MODIFIED) {
                    wprintf(L"file modifide: %s\n", path.c_str());
                } else if (fni->Action == FILE_ACTION_REMOVED) {
                    wprintf(L"file remove: %s\n", path.c_str());
                } else if (fni->Action == FILE_ACTION_ADDED) {
                    wprintf(L"file created: %s\n", path.c_str());
                } else if (fni->Action == FILE_ACTION_RENAMED_OLD_NAME) {
                    wprintf(L"file rename old name: %s\n", path.c_str());
                } else if (fni->Action == FILE_ACTION_RENAMED_NEW_NAME) {
                    wprintf(L"file rename new name: %s\n", path.c_str());
                }

                fni = (PFILE_NOTIFY_INFORMATION)((LPBYTE)fni + cbOffset);
            } while (cbOffset);
        }
        ReadDirectoryChangesW(di.hDir, di.lpBuffer, 4096, TRUE, notifyFilter, &di.dwBufLength, &di.Overlapped, NULL);
    }
    /// async call PostQueuedCompletionStatus(hCompPort, 0, 0, NULL);
    CloseHandle(di.hDir);

    CloseHandle(hCompPort);
}
