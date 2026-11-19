<?php
header("Content-Type: application/json");
echo json_encode(["fallback" => true, "uri" => $_SERVER["REQUEST_URI"]]);
