<?php
header("Content-Type: application/json");
echo json_encode(["CUSTOM_VAR" => $_SERVER["CUSTOM_VAR"] ?? "(not set)"]);
