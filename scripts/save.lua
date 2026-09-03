-- Serialize the settings table without changing it.  The temporary globals
-- are recreated for every call because Lua 4 does not support the lexical
-- closures used by newer Lua versions.

function _save_key (key)
  if type(key) == "string" then
    return format("[%q]", key)
  elseif type(key) == "number" then
    return "["..tostring(key).."]"
  end
end

function _save_append (value)
  _save_output = _save_output..value
end

function _save_value (path, value)
  local value_type = type(value)

  if value == nil or value_type == "userdata" or value_type == "function" then
    return
  end

  _save_append(path.." = ")
  if value_type == "string" then
    _save_append(format("%q", value).."\n")
  elseif value_type == "table" then
    if _save_tables[value] ~= nil then
      _save_append(_save_tables[value].."\n")
    else
      _save_append("{ }\n")
      _save_tables[value] = path
      for key, field in value do
        local key_path = _save_key(key)
        if key_path ~= nil then
          _save_value(path..key_path, field)
        end
      end
    end
  else
    _save_append(tostring(value).."\n")
  end
end

function _save_root (key, value)
  local key_path = _save_key(key)
  if key_path ~= nil and
     (type(key) ~= "string" or strsub(key, 1, 1) ~= "_") then
    _save_value("settings"..key_path, value)
  end
end

function save ()
  local result

  _save_output = ""
  _save_tables = { }
  _save_tables[settings] = "settings"
  foreach(settings, _save_root)

  result = _save_output.."save_completed = 1\n"
  _save_output = nil
  _save_tables = nil
  return result
end
