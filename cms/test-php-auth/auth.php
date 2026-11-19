<?php
header("Content-Type: application/json");
$auth = $_SERVER["HTTP_AUTHORIZATION"] ?? "(not set)";
echo json_encode(["HTTP_AUTHORIZATION" => $auth, "received" => $auth !== "(not set)"]);
