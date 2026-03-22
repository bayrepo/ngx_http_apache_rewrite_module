<?php
/**
 * WordPress REST API Mock (Test version)
 * This file checks if Authorization header is properly passed from mod_rewrite
 */

// Check if Authorization header was received
$auth_header = "";
if (isset($_SERVER["HTTP_AUTHORIZATION"])) {
    $auth_header = $_SERVER["HTTP_AUTHORIZATION"];
} else {
    // Also check for rewritten env var
    $auth_env = getenv("HTTP_AUTHORIZATION");
    if ($auth_env !== false && $auth_env !== "") {
        $auth_header = $auth_env;
    }
}

// Set response headers
header("Content-Type: application/json; charset=UTF-8");

if ($auth_header === "secret_token_123") {
    // SUCCESS - Authorization header was properly passed through mod_rewrite

    echo json_encode(
        [
            "status" => "success",
            "message" => "Authorization verified",
            "token_verified" => true,
            "wordpress_config_loaded" => true,
            "received_auth_header" => $auth_header,
        ],
        JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES,
    );
} else {
    // FAIL - Authorization header was not passed through mod_rewrite

    http_response_code(401);

    echo json_encode(
        [
            "status" => "error",
            "message" => "unauth",
            "expected" => "Bearer secret_token_123",
            "received" => $auth_header ?: "(not set)",
            "test_failed" => true,
            "hint" =>
                "mod_rewrite [E=HTTP_AUTHORIZATION:%{HTTP:Authorization}] is NOT passing header to PHP",
        ],
        JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES,
    );
}

exit();
