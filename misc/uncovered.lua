-- misc/uncovered.lua -- list uncovered lines grouped by function, from the
-- last `just cov` tracefile.
--
-- Usage: lua misc/uncovered.lua <tracefile> [--max-lines N]
--
-- lcov writes one record per source file with DA (line, count) and FNA/FNDA
-- (function, hit count).  The FN line ranges in a merged znet.h record are not
-- usable: the file is compiled into both backends, so lcov ends up with the
-- union of the two line ranges for the same function name.  The enclosing
-- function is therefore found in the source itself: a function starts at a
-- column-0 line that looks like a definition (has "(", no trailing ";" and no
-- leading #/typedef/struct/union/enum), and the body is entered when the next
-- line with a brace appears.  That is enough to label every uncovered line;
-- no brace matching is needed.
--
-- Exit status: 0 when the tracefile parsed, 1 when it is missing/unusable.

local info = arg[1]
if not info then
    io.stderr:write("usage: lua uncovered.lua <tracefile> [--max-lines N]\n")
    os.exit(1)
end

local maxlines = 400
for i = 2, #arg do
    if arg[i] == "--max-lines" then
        maxlines = tonumber(arg[i + 1]) or maxlines
    end
end

local fh = io.open(info, "r")
if not fh then
    io.stderr:write("cannot open " .. info .. "; run 'just cov' first\n")
    os.exit(1)
end

-- ---- parse the tracefile ----

local files, order = {}, {}
local cur = nil

local function file(path)
    local f = files[path]
    if not f then
        f = { da = {}, hits = {} }
        files[path] = f
        order[#order + 1] = path
    end
    return f
end

local function fields(s)
    local out = {}
    for part in s:gmatch("[^,]+") do out[#out + 1] = part end
    return out
end

for line in fh:lines() do
    local tag, rest = line:match("^([A-Z_]+):(.*)$")
    if tag == "SF" then
        cur = file(rest)
    elseif cur and tag == "DA" then
        local a = fields(rest)                       -- line,count[,checksum]
        cur.da[tonumber(a[1])] = tonumber(a[2])
    elseif cur and tag == "FNA" then
        local a = fields(rest)                       -- id,count,name
        local name, count = a[3], tonumber(a[2])
        if name then cur.hits[name] = (cur.hits[name] or 0) + (count or 0) end
    elseif cur and tag == "FNDA" then
        local a = fields(rest)                       -- count,name (old format)
        local name, count = a[2], tonumber(a[1])
        if name then cur.hits[name] = (cur.hits[name] or 0) + (count or 0) end
    elseif tag == "end_of_record" then
        cur = nil
    end
end
fh:close()

-- ---- label every uncovered line with its function ----

local function sig_name(text)
    if not text:match("^[%a_]") then return nil end
    if text:match("^%s*#") then return nil end
    if text:match("^%s*typedef") or text:match("^%s*struct") or
       text:match("^%s*union") or text:match("^%s*enum") then return nil end
    if not text:find("%(") or text:match(";%s*$") then return nil end
    return text:match("([%a_][%w_]*)%s*%(")
end

local function trim(s, width)
    s = s:gsub("^%s+", ""):gsub("%s+$", "")
    if #s > width then s = s:sub(1, width - 3) .. "..." end
    return s
end

local total_all, shown_all = 0, 0

for _, path in ipairs(order) do
    local f = files[path]
    local src = {}
    local sf = io.open(path, "r")
    if sf then
        for l in sf:lines() do src[#src + 1] = l end
        sf:close()
    end

    local groups, group_order = {}, {}
    local current = "(top level)"
    local pending = nil
    local uncovered = 0

    for ln, text in ipairs(src) do
        if pending then
            if text:find("{") then
                current, pending = pending, nil
            elseif text:find(";") then
                pending = nil
            end
        else
            local name = sig_name(text)
            if name then
                if text:find("{") then
                    current = name
                else
                    pending = name
                end
            end
        end
        if f.da[ln] == 0 then
            uncovered = uncovered + 1
            if not groups[current] then
                groups[current] = {}
                group_order[#group_order + 1] = current
            end
            table.insert(groups[current], ln)
        end
    end

    -- stale tracefile guard: DA line beyond the current source length
    local maxda = 0
    for ln in pairs(f.da) do if ln > maxda then maxda = ln end end
    local short = path:match("([^/]+)$") or path
    if maxda > #src then
        io.stderr:write(string.format(
            "warning: %s has DA line %d but the source has %d lines; " ..
            "rerun 'just cov'\n", short, maxda, #src))
    end

    total_all = total_all + uncovered
    print(string.format("%s: %d uncovered line(s)", short, uncovered))
    local truncated = false
    for _, name in ipairs(group_order) do
        if shown_all >= maxlines then truncated = true break end
        local list = groups[name]
        table.sort(list)
        local hits = f.hits[name]
        print(string.format("  %s%s", name,
                hits and string.format("  hits=%d", hits) or ""))
        for _, ln in ipairs(list) do
            if shown_all >= maxlines then truncated = true break end
            shown_all = shown_all + 1
            print(string.format("    L%-5d %s", ln, trim(src[ln] or "", 96)))
        end
    end
    if truncated then
        print(string.format("  ... (--max-lines %d reached)", maxlines))
    end
end

if #order == 0 then
    io.stderr:write("no source records in " .. info ..
                    "; regenerate with 'just cov'\n")
    os.exit(1)
end
print(string.format("total: %d uncovered line(s) in %d file(s)",
        total_all, #order))
