-- post_large.lua
-- wrk script for large-payload POST /echo (~4 KB body)
-- Tests how well each framework handles request body parsing under high load.
--
-- Usage: wrk -t4 -c100 -d20s --latency -s post_large.lua http://HOST/echo

wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"

-- Build a ~4 KB JSON payload once at init time
local function make_large_body()
    local items = {}
    for i = 1, 40 do
        items[i] = string.format(
            '{"id":%d,"name":"item-%d","value":"payload-data-%d-abcdefghijklmnopqrstuvwxyz-0123456789"}',
            i, i, i
        )
    end
    return '{"items":[' .. table.concat(items, ",") .. '],"total":40,"page":1,"pageSize":40}'
end

local LARGE_BODY = make_large_body()

-- wrk calls init() once per thread
function init(args)
    wrk.body = LARGE_BODY
end

function request()
    return wrk.format("POST", nil, nil, LARGE_BODY)
end
