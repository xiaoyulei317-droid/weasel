-- 雙語拼音: pick a sentence word by word, then type it as Chinese or English.
--
-- Rime types a sentence as soon as its last word is picked. Once a word has
-- been picked on its own, this holds the finished sentence instead, so all
-- of it can be typed at the end: as Chinese with space or Enter, or as its
-- English translation (on Windows, Ctrl+Enter or a click on the translation
-- line). A sentence picked in one go is typed right away, as before.
-- Backspace on a held sentence takes back its last word, to pick it again.
--
-- Only while the schema's `english` switch is on.
--
-- Rime's editor reads the hidden option `_auto_commit` to choose between
-- typing a finished sentence and holding it; this keeps that option in step.

local kAccepted = 1
local kNoop = 2
local XK_BackSpace = 0xff08

local function sync(ctx)
  local hold = ctx:get_option("english") and
      ctx.composition:toSegmentation():get_confirmed_position() > 0
  if ctx:get_option("_auto_commit") == hold then
    ctx:set_option("_auto_commit", not hold)
  end
end

-- Every word is picked and the sentence waits.
local function held(ctx)
  local length = #ctx.input
  return ctx:is_composing() and length > 0 and ctx.caret_pos == length and
      ctx.composition:toSegmentation():get_confirmed_position() == length
end

local M = {}

function M.init(env)
  local ctx = env.engine.context
  -- A pick by mouse or touch skips the processors, so also follow picks
  -- (Rime has handled the pick by then), and picks taken back.
  env.select_connection = ctx.select_notifier:connect(sync)
  env.update_connection = ctx.update_notifier:connect(sync)
end

function M.fini(env)
  if env.select_connection then env.select_connection:disconnect() end
  if env.update_connection then env.update_connection:disconnect() end
end

function M.func(key, env)
  local ctx = env.engine.context
  sync(ctx)
  -- Rime would delete the last letter of the last word instead.
  if key.keycode == XK_BackSpace and key.modifier == 0 and held(ctx) then
    ctx:reopen_previous_segment()
    return kAccepted
  end
  return kNoop
end

return M
