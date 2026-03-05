-- post_echo.lua
-- wrk script for Scenario C: POST /echo
-- Sends a realistic REST API payload (~200 bytes JSON)
--
-- Usage: wrk -t4 -c100 -d20s --latency -s post_echo.lua http://HOST/echo

wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"
wrk.body = '{"id":42,"name":"benchmark-test","active":true,"score":9.87,"tags":["http","performance","rest"],"meta":{"version":"1.0","timestamp":1709600000}}'
