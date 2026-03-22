<?php
/**
 * WordPress Multisite - index.php (Test version)
 * This file handles routing for non-existing files/directories in multisite setup
 */

// Simulated WordPress Multisite response
echo "<html><head><title>WordPress Multisite Test Site</title></head><body>";
echo "<h1>WordPress Multisite Content Route</h1>";
echo "<p>This page is served by index.php via RewriteRule.</p>";
echo "<div class='wp-multisite-config'>WordPress Multisite Configuration Loaded</div>";
echo "</body></html>";

// Exit
exit;
