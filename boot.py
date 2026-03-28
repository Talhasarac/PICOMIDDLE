import usb_host
import board

# Switch native USB port to host mode so keyboard can be plugged in.
# This disables the CircuitPython REPL/USB drive — power via VSYS pin instead.
usb_host.Port(board.USB_HOST_DP, board.USB_HOST_DM)
