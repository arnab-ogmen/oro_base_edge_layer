-- Seed the local database with the default development device
INSERT INTO oro_base_device (
    device_id, 
    device_name, 
    serial_number, 
    model, 
    status, 
    is_active
) VALUES (
    '9e092b69-5973-46e4-a228-fe4933e04364', 
    'Radxa-Cubie-Dev', 
    'ORO-DEV-001', 
    'Cubie A7z', 
    'online', 
    true
) ON CONFLICT (device_id) DO NOTHING;
