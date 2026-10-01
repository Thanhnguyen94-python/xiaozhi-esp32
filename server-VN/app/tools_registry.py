from dataclasses import dataclass
from typing import Any


@dataclass
class ToolItem:
    name: str
    description: str


class ToolRegistry:
    """Simple paged tool registry to avoid hard 32-tools cap on one response."""

    def __init__(self) -> None:
        self._tools: list[ToolItem] = self._build_default_tools()

    @staticmethod
    def _build_default_tools() -> list[ToolItem]:
        names = [
            'self.get_device_status',
            'self.robot.move',
            'self.robot.control',
            'self.webui.get_access_info',
            'self.webui.get_ip',
            'self.webui.get_login_link',
            'self.audio_speaker.set_volume',
            'self.audio_speaker.volume_up',
            'self.audio_speaker.volume_down',
            'self.audio_speaker.mute',
            'self.audio_speaker.unmute',
            'self.audio_speaker.get_status',
            'self.lamp.on',
            'self.lamp.off',
            'self.lamp.set_brightness',
            'self.robot.wheels.forward',
            'self.robot.wheels.backward',
            'self.robot.wheels.left',
            'self.robot.wheels.right',
            'self.robot.wheels.stop',
            'self.robot.wheels.speed_up',
            'self.robot.wheels.speed_down',
            'self.robot.head_servo.center',
            'self.robot.head_servo.left',
            'self.robot.head_servo.right',
            'self.robot.head_servo.up',
            'self.robot.head_servo.down',
            'self.robot.head_servo.nod',
            'self.robot.head_servo.shake',
            'self.robot.head_servo.curious',
            'self.robot.head_servo.wave',
            'self.robot.head_servo.sleep',
            'self.robot.head_servo.wake',
            'self.robot.head_servo.reset',
            'self.screen.set_theme',
            'self.screen.brightness',
            'self.network.reconnect',
            'self.network.get_status',
            'self.power.get_battery',
            'self.power.sleep',
            'self.diagnostics.get_logs',
            'self.diagnostics.get_metrics',
        ]
        return [ToolItem(name=n, description=f'Tool: {n}') for n in names]

    def list_tools(self, cursor: int = 0, limit: int = 16) -> dict[str, Any]:
        safe_cursor = max(0, cursor)
        safe_limit = max(1, min(limit, 64))

        chunk = self._tools[safe_cursor:safe_cursor + safe_limit]
        next_cursor = safe_cursor + safe_limit
        has_more = next_cursor < len(self._tools)

        return {
            'items': [{'name': t.name, 'description': t.description} for t in chunk],
            'next_cursor': next_cursor if has_more else None,
            'total': len(self._tools),
        }

    def call_tool(self, name: str, arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        args = arguments or {}

        if name == 'self.get_device_status':
            return {
                'ok': True,
                'result': {
                    'board': 'bread-compact-wifi',
                    'network': {'connected': True},
                    'note': 'VN server mock status. Replace with real integration when needed.',
                },
            }

        if any(t.name == name for t in self._tools):
            return {
                'ok': True,
                'result': {
                    'tool': name,
                    'accepted_arguments': args,
                    'note': 'Tool call accepted by VN server. Implement hardware bridge if required.',
                },
            }

        return {'ok': False, 'error': f'Unknown tool: {name}'}
