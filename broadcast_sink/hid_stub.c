#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <string.h>

static const uint8_t report_map[] = {
	0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
	0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
	0x95, 0x08, 0x81, 0x02, 0xC0,
};

static ssize_t read_report_map(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr,
			       void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				report_map, sizeof(report_map));
}

static const uint8_t hid_info[] = { 0x11, 0x01, 0x00, 0x02 };

static ssize_t read_hid_info(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				hid_info, sizeof(hid_info));
}

static uint8_t ctrl_point;

static ssize_t write_ctrl_point(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len,
				uint16_t offset, uint8_t flags)
{
	if (len != 1) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	memcpy(&ctrl_point, buf, 1);
	return len;
}

BT_GATT_SERVICE_DEFINE(hid_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ,
			       read_hid_info, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ,
			       read_report_map, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT,
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE,
			       NULL, write_ctrl_point, &ctrl_point),
);
