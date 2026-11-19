<?php
header("Content-Type: application/json");
echo json_encode([
    "REQUEST_URI" => $_SERVER["REQUEST_URI"] ?? "",
    "QUERY_STRING" => $_SERVER["QUERY_STRING"] ?? "",
    "REQUEST_METHOD" => $_SERVER["REQUEST_METHOD"] ?? "",
    "HTTP_HOST" => $_SERVER["HTTP_HOST"] ?? "",
    "DOCUMENT_ROOT" => $_SERVER["DOCUMENT_ROOT"] ?? "",
    "SCRIPT_FILENAME" => $_SERVER["SCRIPT_FILENAME"] ?? "",
    "REQUEST_FILENAME" => $_SERVER["REQUEST_FILENAME"] ?? "",
    "SERVER_PORT" => $_SERVER["SERVER_PORT"] ?? "",
    "SERVER_PROTOCOL" => $_SERVER["SERVER_PROTOCOL"] ?? "",
    "REMOTE_ADDR" => $_SERVER["REMOTE_ADDR"] ?? "",
    "HTTPS" => $_SERVER["HTTPS"] ?? "off",
]);
