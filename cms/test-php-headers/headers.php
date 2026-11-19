<?php
header("Content-Type: application/json");
echo json_encode(["HTTP_X_CUSTOM" => $_SERVER["HTTP_X_CUSTOM"] ?? "(not set)"]);
