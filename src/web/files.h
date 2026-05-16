#pragma once

// Mounts the home page (`/`) and the file/folder browser (`/files`) plus the
// mutating endpoints they use: /del, /mkdir, /rmdir, /move, /jumppage.
void registerFilesRoutes();
