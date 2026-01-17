#include "Prerender.h"

#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <filesystem>
#include <system_error>
#include <stdexcept>
#include <errno.h>
#include <string>

namespace fs = std::filesystem;

namespace Prerender
{
 
    static const char *NODE_PATH = std::getenv("NODE_PATH");
    static const char *PRERENDER_SCRIPT = std::getenv("PRERENDER_SCRIPT");

    void atomic_folder_swap(const fs::path& stagingDir,
                            const fs::path& finalDir,
                            const fs::path& backupDir) {
        std::error_code ec;

        // 1. Basic sanity checks
        if (!fs::exists(stagingDir, ec) || !fs::is_directory(stagingDir, ec)) {
            throw AtomicFolderSwapError("Staging directory does not exist or is not a directory: " +
                                        stagingDir.string());
        }

        // Optional: ensure finalDir parent exists
        fs::create_directories(finalDir.parent_path(), ec);
        if (ec) {
            throw AtomicFolderSwapError("Failed to create parent dirs for finalDir: " +
                                        finalDir.parent_path().string() + " : " + ec.message());
        }

        // 2. Remove any previous leftover backup
        if (fs::exists(backupDir, ec)) {
            fs::remove_all(backupDir, ec); // best effort; on error we still continue
        }

        // 3. If finalDir exists, move it to backupDir
        if (fs::exists(finalDir, ec)) {
            fs::rename(finalDir, backupDir, ec);
            if (ec) {
                throw AtomicFolderSwapError("Failed to rename finalDir to backupDir: " +
                                            finalDir.string() + " -> " + backupDir.string() +
                                            " : " + ec.message());
            }
        }

        // 4. Rename stagingDir -> finalDir (atomic on same filesystem)
        fs::rename(stagingDir, finalDir, ec);
        if (ec) {
            // Try to roll back: move backup back to finalDir
            std::error_code rollbackEc;
            if (fs::exists(backupDir, rollbackEc)) {
                fs::rename(backupDir, finalDir, rollbackEc);
            }
            throw AtomicFolderSwapError("Failed to rename stagingDir to finalDir: " +
                                        stagingDir.string() + " -> " + finalDir.string() +
                                        " : " + ec.message());
        }

        // 5. Cleanup: remove backupDir (best effort)
        if (fs::exists(backupDir, ec)) {
            fs::remove_all(backupDir, ec); // ignore errors
        }
    }

    void swap_single_post(const PrerenderResult& r) {
        fs::path staging = r.stagingDir;             // e.g. /var/www/site-staging/posts/1234
        fs::path final   = r.finalDir;               // e.g. /var/www/site/posts/1234
        fs::path backup  = final;
        backup += ".bak";                            // /var/www/site/posts/1234.bak

        atomic_folder_swap(staging, final, backup);
    }

    bool write_all(int fd, const void* data, size_t size) {
        const char* buf = static_cast<const char*>(data);
        size_t total_written = 0;
        bool result = true;
        while (total_written < size) {
            ssize_t n = write(fd, buf + total_written, size - total_written);

            if (n == -1) {
                if (errno == EINTR) {
                    continue; // retry
                }
                result = false;
                throw std::runtime_error("write() failed: " + std::string(strerror(errno)));
            }

            if (n == 0) {
                result = false;
                throw std::runtime_error("write() returned 0 (pipe closed)");
            }

            total_written += n;
        }
        return result;
    }

    void prerenderPost(const std::string& jsonData) {
        int pipe_in[2];   // parent -> child
        int pipe_out[2];  // child -> parent

        if (pipe(pipe_in) == -1) {
            perror("pipe_in failed");
            return;
        }

        if (pipe(pipe_out) == -1) {
            perror("pipe_out failed");
            close(pipe_in[0]);
            close(pipe_in[1]);
            return;
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
            close(pipe_in[0]); close(pipe_in[1]);
            close(pipe_out[0]); close(pipe_out[1]);
            return;
        }

        if (pid == 0) {
            // -------------------------
            // CHILD PROCESS
            // -------------------------

            // Close parent's ends
            close(pipe_in[1]);
            close(pipe_out[0]);

            // Redirect stdin/stdout
            dup2(pipe_in[0], STDIN_FILENO);
            dup2(pipe_out[1], STDOUT_FILENO);

            close(pipe_in[0]);
            close(pipe_out[1]);

            // 🔒 Close all other inherited fds
            for (int fd = 3; fd < 1024; fd++) {
                close(fd);
            }

            // Prepare exec args
            char* argv[] = {
                const_cast<char*>("node"),
                const_cast<char*>(PRERENDER_SCRIPT),
                nullptr
            };

            std::string pagefolderEnv = "PAGE_FOLDER=";// + pagefolder;
            char* envp[] = { const_cast<char*>(pagefolderEnv.c_str()), nullptr };

            execve(NODE_PATH, argv, envp);

            _exit(1); // exec failed
        }

        // -------------------------
        // PARENT PROCESS
        // -------------------------

        close(pipe_in[0]);   // parent writes only
        close(pipe_out[1]);  // parent reads only

        // Build payload
        //std::string payload = "{\"pagefolder\":\"" + pagefolder + "\",\"pagedata\":" + jsonData + "}";
        std::string payload = "{\"pagedata\":" + jsonData + "}";

        // Write full payload
        if (!write_all(pipe_in[1], payload.c_str(), payload.size())) {
            std::cerr << "Failed to write prerender payload\n";
        }

        close(pipe_in[1]); // send EOF to child

        // -------------------------
        // Read child's output directly from pipe
        // -------------------------

        std::string output;
        char buffer[4096];
        ssize_t n;

        while ((n = read(pipe_out[0], buffer, sizeof(buffer))) > 0) {
            output.append(buffer, n);
        }

        close(pipe_out[0]);

        if (output.empty()) {
            std::cerr << "No data received from prerender process\n";
            throw std::string("No data received from prerender process");
        }

        // Parse JSON result with serialized nholman json from_json()
        PipeResponse<PrerenderResult> v;
        json responseJson = json::parse(output);
        v = response<PrerenderResult>(responseJson);
        if (std::holds_alternative<PipeError>(v))
        {
          auto error = std::get<PipeError>(v);
          throw std::string(error.message);
        }

        PrerenderResult result = std::get<PrerenderResult>(v);
        // Wait for child
        int status;
        waitpid(pid, &status, 0);

        // Use result
        swap_single_post(result);
        std::cout << "prerenderPost result: " << std::endl
                  << "ok " << result.ok << std::endl
                  << "slug " << result.slug << std::endl
                  << "route " << result.route << std::endl
                  << "finalDir " << result.finalDir << std::endl
                  << "stagingDir " << result.stagingDir << std::endl
                  << std::endl;

        

    }

}