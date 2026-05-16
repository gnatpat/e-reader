#pragma once

// Mounts /upload (book upload, multipart) and /upload-sleep (custom sleep
// image upload). Both register a stream handler (chunk receiver) and a done
// handler (final response).
void registerUploadRoutes();
