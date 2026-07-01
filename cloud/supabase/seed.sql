-- Seed do dispositivo de demonstração (Chunk F).
insert into public.devices (device_id, name, location, thresholds)
values ('tmt-dev-01', 'Geladeira Lab', 'Laboratório TMT',
        '{"temp_min_c": 0, "temp_max_c": 23}'::jsonb)
on conflict (device_id) do nothing;
