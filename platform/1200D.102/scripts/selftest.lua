-- 1200D (T5) self-test: run from Modules tab > Lua > Run script 'selftest'
-- Automates the flash-cycle chores: settings snapshot, A/B audio clips,
-- EDMAC photo-raw monitor run, sustained-rate test clip.
-- Everything lands in ML/LOGS/SELFTEST.TXT; analysis happens on the PC
-- (mlv_ingest.sh prints fps + audio loudness per clip).

local function log(fmt, ...)
    local ok, f = pcall(io.open, "ML/LOGS/SELFTEST.TXT", "a")
    if not ok or not f then return end
    local ok2, s = pcall(string.format, fmt, ...)
    f:write(os.date("!%Y-%m-%dT%H:%M:%S ") .. (ok2 and s or fmt) .. "\r\n")
    f:close()
end

local function snapshot(tag)
    log("--- %s ---", tag)
    local env = {
        {"camera.model",      function() return camera.model end},
        {"camera.firmware",   function() return camera.firmware end},
        {"lens.name",         function() return lens.name end},
        {"battery.level",     function() return tostring(battery.level) end},
        {"ml.version",        function() return ml.version end},
        {"lv.running",        function() return tostring(lv.running) end},
        {"lv.zoom",           function() return tostring(lv.zoom) end},
    }
    for _, e in ipairs(env) do
        local ok, v = pcall(e[2])
        log("%-18s %s", e[1], ok and tostring(v) or "n/a")
    end
    -- audio + raw menu snapshot (names that exist on the 1200D lapis audio)
    local menus = {
        {"Audio", {"AGC", "Input source", "Wind Filter", "Audio Meters",
                    "Left Digital Gain", "Right Digital Gain",
                    "Enable audio", "Enable audio override", "Enable", "Override audio"}},
        {"Movie", {"RAW video", "Card warm-up [FPS x2]", "Data rate"}},
        {"Debug", {"Photo EDMAC monitor (1200D)"}},
    }
    for _, m in ipairs(menus) do
        for _, entry in ipairs(m[2]) do
            local ok, v = pcall(menu.get, m[1], entry)
            if ok then log("menu %s / %s = %s", m[1], entry, tostring(v)) end
        end
    end
end

local function record_clip(seconds, tag)
    local ok, err = pcall(movie.start)
    if not ok then
        log("CLIP %s: movie.start failed: %s", tag, tostring(err))
        return false
    end
    msleep(seconds * 1000)
    pcall(movie.stop)
    msleep(2000) -- let the indexer settle
    log("CLIP %s: recorded %ds", tag, seconds)
    return true
end

local function set_audio(override_on)
    -- returns list of what was actually changed; tries known entry-name variants
    local changed = {}
    local tries = {
        {"AGC", override_on and 0 or 1}, -- AGC off while override on
        {"Enable audio", override_on and 1 or 0},
        {"Enable audio override", override_on and 1 or 0},
        {"Override audio", override_on and 1 or 0},
    }
    for _, t in ipairs(tries) do
        local ok = pcall(menu.set, "Audio", t[1], t[2])
        if ok then changed[#changed+1] = string.format("%s=%s", t[1], tostring(t[2])) end
    end
    if #changed == 0 then
        log("AUDIO: no master override entry found — toggle manually between runs")
    else
        log("AUDIO set (%s): %s", override_on and "OVERRIDE ON" or "override OFF", table.concat(changed, ", "))
    end
    return #changed > 0
end

--------------------------------------------------------------------
log("=========== 1200D SELF-TEST START ===========")
snapshot("environment")

-- 1) A/B audio test: two 8s clips, override OFF then ON (if toggleable)
local ok_toggle = set_audio(false)
if record_clip(8, "A-override-off") then
    if ok_toggle then
        set_audio(true)
        record_clip(8, "B-override-on")
        set_audio(false) -- leave camera in stock state
    else
        log("AUDIO A/B: B leg skipped (no menu control) — run again with manual toggle")
    end
else
    log("AUDIO A/B: skipped (not in movie mode?) — switch mode dial to Movie and rerun")
end

-- 2) EDMAC photo-raw monitor: arm monitor, shoot one RAW photo, wait out the 30s window
local ok_sel = pcall(menu.select, "Debug", "Photo EDMAC monitor (1200D)")
log("EDMAC monitor armed: %s", tostring(ok_sel))
if ok_sel then
    msleep(1000)
    local ok_shoot, err = pcall(camera.shoot, {wait = true})
    log("EDMAC test photo: %s %s", tostring(ok_shoot), not ok_shoot and tostring(err) or "")
    log("waiting 35s for monitor window...")
    msleep(35000)
    log("EDMAC monitor done — see ML/LOGS/EDMACMon.TXT")
end

-- 3) Sustained-rate clip: 20s at current raw settings; offline: ingest reports
--    achieved fps + total bytes -> effective card data rate in real conditions
record_clip(20, "sustained-rate")

snapshot("final state")
log("=========== SELF-TEST END ===========")
log("NEXT ON PC: bash ~/scripts/mlv_ingest.sh <card-dir>  (reads ML/LOGS too)")

console.show()
print("Self-test complete.")
print("Pull card -> mlv_ingest.sh")
