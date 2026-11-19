<?php
/**
 * Header duplicate check - verifies that E=VAR:VAL does not create
 * duplicate headers in $_SERVER when a real client header already exists.
 */
header("Content-Type: application/json; charset=UTF-8");

$result = array();

// Count how many times HTTP_AUTHORIZATION appears in $_SERVER
$auth_count = 0;
foreach ($_SERVER as $key => $value) {
    if ($key === "HTTP_AUTHORIZATION") {
        $auth_count++;
        $result["HTTP_AUTHORIZATION_value"] = $value;
    }
}

$result["HTTP_AUTHORIZATION_count"] = $auth_count;
$result["no_duplicates"] = ($auth_count <= 1);

echo json_encode($result, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
exit();
