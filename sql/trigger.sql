CREATE OR REPLACE FUNCTION notify_table_change()
RETURNS TRIGGER AS $$
BEGIN
  -- Send a JSON payload to the 'table_updates' channel
  PERFORM pg_notify('table_updates', json_build_object(
    'table', TG_TABLE_NAME,
    'action', TG_OP,
    'data', row_to_json(NEW)
  )::text);
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_after_insert
AFTER INSERT ON your_table
FOR EACH ROW EXECUTE FUNCTION notify_table_change();
