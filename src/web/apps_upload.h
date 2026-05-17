#pragma once

// Mounts /upload-app — multipart streaming receiver for Pala app .bin
// binaries. Files land in /apps/. The handler validates the
// PalaAppHeader magic before committing the temp file, so a non-app
// upload is rejected before it can pollute the catalog.
//
// Call once from registerWebRoutes().
void registerAppUploadRoutes();
