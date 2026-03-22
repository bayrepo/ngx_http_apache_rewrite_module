<?php
/**
 * WordPress - index.php (Test version)
 * This file handles routing for non-existing files/directories
 */

// Simulated WordPress response
echo "<html><head><title>WordPress Test Site</title></head><body>";
echo "<h1>WordPress Content Route</h1>";
echo "<p>This page is served by index.php via RewriteRule.</p>";
echo "<div class='wp-config'>WordPress Configuration Loaded</div>";
echo "</body></html>";

// Exit
exit;
