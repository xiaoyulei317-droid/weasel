-- 雙語拼音: English translations for Rime candidates.
--
-- For every Chinese candidate with a dictionary translation, the first English
-- option is shown as the candidate's comment. Right after the first candidate,
-- its English options are offered as candidates of their own, so picking one
-- (number key, tap or click) types the English word instead of the Chinese.
--
-- English the user picks is remembered (bilingual_usage.txt in the user
-- folder), so a word's usual translation comes first next time.
--
-- Turned on and off by the schema's `english` switch.
-- Options under the filter's name space in the schema:
--   english_options: how many English candidates follow the first candidate (default 3)

local table_path = "bilingual_english.txt"
local usage_path = "bilingual_usage.txt"
local words = nil  -- word -> "opt1|opt2|..." (shared by every session)
local usage = nil  -- word -> { english -> times picked }

local function open_table()
  local dirs = { rime_api.get_user_data_dir(), rime_api.get_shared_data_dir() }
  for _, dir in ipairs(dirs) do
    local f = io.open(dir .. "/" .. table_path, "r")
    if f then return f end
  end
  return nil
end

local function load_table()
  if words then return end
  words = {}
  local f = open_table()
  if not f then
    log.warning("bilingual_english: " .. table_path .. " not found")
    return
  end
  for line in f:lines() do
    local word, opts = line:match("^([^\t]+)\t(.+)$")
    if word then words[word] = opts end
  end
  f:close()
end

-- The usage file is a log: one "word \t english" line per pick.
local function load_usage()
  if usage then return end
  usage = {}
  local f = io.open(rime_api.get_user_data_dir() .. "/" .. usage_path, "r")
  if not f then return end
  for line in f:lines() do
    local word, en = line:match("^([^\t]+)\t([^\t\r]+)")
    if word then
      local counts = usage[word] or {}
      counts[en] = (counts[en] or 0) + 1
      usage[word] = counts
    end
  end
  f:close()
end

local function record(word, en)
  local counts = usage[word] or {}
  counts[en] = (counts[en] or 0) + 1
  usage[word] = counts
  local f = io.open(rime_api.get_user_data_dir() .. "/" .. usage_path, "a")
  if f then
    f:write(word, "\t", en, "\n")
    f:close()
  end
end

-- The options of a word, the ones picked most often first.
local function options_of(word)
  local opts = words[word]
  if not opts then return nil end
  local list = {}
  for o in opts:gmatch("[^|]+") do list[#list + 1] = o end
  local counts = usage[word]
  if counts then
    local order = {}
    for i, o in ipairs(list) do order[o] = i end
    table.sort(list, function(a, b)
      local ca, cb = counts[a] or 0, counts[b] or 0
      if ca ~= cb then return ca > cb end
      return order[a] < order[b]
    end)
  end
  return list
end

local M = {}

function M.init(env)
  load_table()
  load_usage()
  local config = env.engine.schema.config
  -- Some librime-lua versions keep the "*" of "lua_filter@*name" in name_space.
  local ns = env.name_space:gsub("^%*", "")
  env.english_options = config:get_int(ns .. "/english_options") or 3
  -- "start|english" -> the Chinese word it translates, for this composition
  env.source = {}
  env.commit_connection = env.engine.context.commit_notifier:connect(function(ctx)
    -- A sentence can be picked word by word, so look at every word of it.
    for _, seg in ipairs(ctx.composition:toSegmentation():get_segments()) do
      local cand = seg:get_selected_candidate()
      if cand and cand.type == "english" then
        local word = env.source[cand.start .. "|" .. cand.text]
        if word then record(word, cand.text) end
      end
    end
    env.source = {}
  end)
end

function M.fini(env)
  if env.commit_connection then env.commit_connection:disconnect() end
end

function M.func(input, env)
  local show = env.engine.context:get_option("english")
  local index = 0
  for cand in input:iter() do
    index = index + 1
    local opts = show and options_of(cand.text) or nil
    if opts then
      -- A candidate in Taiwan forms (這裏 -> 這裡) is a shadow of the
      -- dictionary's, which takes no comment of its own but shows its
      -- original's.
      local target = cand:get_genuine()
      local hint = opts[1]
      if target.comment == "" then
        target.comment = hint
      else
        target.comment = target.comment .. " " .. hint
      end
    end
    yield(cand)
    if opts and index == 1 then
      for n, o in ipairs(opts) do
        if n > env.english_options then break end
        local en = Candidate("english", cand.start, cand._end, o, "〔英〕")
        en.quality = cand.quality
        env.source[cand.start .. "|" .. o] = cand.text
        yield(en)
      end
    end
  end
end

return M
