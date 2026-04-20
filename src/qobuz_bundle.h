#pragma once
#include <string>
#include <vector>

// Credentials scraped from the Qobuz web-player bundle.js.
// app_id and secrets rotate when Qobuz updates their web player, so they
// must be fetched dynamically rather than stored persistently.
struct BundleCredentials {
    std::string app_id;
    std::vector<std::string> secrets;  // ordered; first working one is used
};

// Fetches a fresh app_id and the list of candidate secrets by downloading
// https://play.qobuz.com/login, locating the bundle.js URL, downloading it,
// and extracting the embedded credentials using the same algorithm as qobuz-dl.
BundleCredentials fetch_bundle_credentials(abort_callback& abort);
