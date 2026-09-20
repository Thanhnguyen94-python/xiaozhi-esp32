import json
import os
from typing import Any, Dict, List, Optional, Set

from fastapi import FastAPI, HTTPException, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse
from dotenv import load_dotenv
import websockets
from yt_dlp import YoutubeDL

# FastMCP import compatibility
try:
    from fastmcp import FastMCP  # type: ignore
except Exception:  # pragma: no cover
    from mcp.server.fastmcp import FastMCP  # type: ignore


APP_NAME = "mcp-youtube-server"
load_dotenv()
MCP_AUTH_TOKEN = os.getenv("MCP_AUTH_TOKEN", "")
UPSTREAM_MCP_WSS = os.getenv("UPSTREAM_MCP_WSS", "").strip()
REQUIRE_UPSTREAM = os.getenv("REQUIRE_UPSTREAM", "false").lower() in ("1", "true", "yes")
YT_MAX_RESULTS = max(1, int(os.getenv("YT_MAX_RESULTS", "5")))
LOCAL_TOOL_NAME = "search_youtube_music"

mcp = FastMCP("YouTube Music Search MCP")
app = FastAPI(title=APP_NAME)


def _pick_mcp_asgi_app() -> Any:
    """Try common FastMCP ASGI builders across versions."""
    candidates = [
        "sse_app",
        "streamable_http_app",
        "http_app",
        "asgi_app",
    ]
    for name in candidates:
        builder = getattr(mcp, name, None)
        if not callable(builder):
            continue

        for kwargs in ({}, {"path": "/"}, {"mount_path": "/"}):
            try:
                return builder(**kwargs)
            except TypeError:
                continue
            except Exception:
                continue
    raise RuntimeError(
        "Cannot build MCP ASGI app from FastMCP instance. "
        "Please check fastmcp/mcp package version."
    )


@app.middleware("http")
async def token_auth_middleware(request: Request, call_next):
    # Protect MCP transport path only; keep health endpoint public.
    if request.url.path.startswith("/sse") or request.url.path.startswith("/mcp"):
        if not MCP_AUTH_TOKEN:
            return JSONResponse(
                status_code=500,
                content={"error": "Server token is not configured (MCP_AUTH_TOKEN)."},
            )
        token = request.query_params.get("token", "")
        if token != MCP_AUTH_TOKEN:
            return JSONResponse(status_code=401, content={"error": "Unauthorized"})
    return await call_next(request)


@app.get("/healthz")
def healthz() -> Dict[str, Any]:
    return {
        "ok": True,
        "service": APP_NAME,
        "upstream_configured": bool(UPSTREAM_MCP_WSS),
        "require_upstream": REQUIRE_UPSTREAM,
    }


@mcp.tool()
def search_youtube_music(song_name: str) -> str:
    """
    Search YouTube music by song title and return normalized JSON string.
    """
    query = (song_name or "").strip()
    if not query:
        return json.dumps(
            {"query": song_name, "total": 0, "results": [], "error": "song_name is empty"},
            ensure_ascii=False,
        )

    ydl_opts = {
        "quiet": True,
        "skip_download": True,
        "noplaylist": True,
        "extract_flat": True,
    }

    results: List[Dict[str, Any]] = []
    try:
        with YoutubeDL(ydl_opts) as ydl:
            info = ydl.extract_info(f"ytsearch{YT_MAX_RESULTS}:{query}", download=False)

        entries = info.get("entries", []) if isinstance(info, dict) else []
        for item in entries:
            if not isinstance(item, dict):
                continue

            video_id = item.get("id")
            webpage_url = item.get("webpage_url")
            if not webpage_url and video_id:
                webpage_url = f"https://www.youtube.com/watch?v={video_id}"

            results.append(
                {
                    "title": item.get("title", ""),
                    "url": webpage_url or "",
                    "duration_seconds": int(item.get("duration") or 0),
                }
            )

        payload = {
            "query": query,
            "total": len(results),
            "results": results,
        }
        return json.dumps(payload, ensure_ascii=False)

    except Exception as exc:
        return json.dumps(
            {"query": query, "total": 0, "results": [], "error": str(exc)},
            ensure_ascii=False,
        )


def _local_tool_schema() -> Dict[str, Any]:
    return {
        "name": LOCAL_TOOL_NAME,
        "description": "Search YouTube music and return title, url, duration in JSON text.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "song_name": {
                    "type": "string",
                    "description": "Song name or keyword to search on YouTube",
                }
            },
            "required": ["song_name"],
        },
    }


def _jsonrpc_ok(request_id: Any, result: Dict[str, Any]) -> Dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request_id, "result": result}


def _jsonrpc_error(request_id: Any, code: int, message: str) -> Dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}


def _make_tools_call_success(request_id: Any, text: str) -> Dict[str, Any]:
    return _jsonrpc_ok(
        request_id,
        {
            "content": [{"type": "text", "text": text}],
            "isError": False,
        },
    )


def _merge_tools_list_result(result_obj: Dict[str, Any]) -> Dict[str, Any]:
    tools = result_obj.get("tools")
    if not isinstance(tools, list):
        tools = []
    if not any(isinstance(t, dict) and t.get("name") == LOCAL_TOOL_NAME for t in tools):
        tools.append(_local_tool_schema())
    result_obj["tools"] = tools
    return result_obj


def _handle_local_request(payload: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    method = payload.get("method")
    request_id = payload.get("id")
    params = payload.get("params") or {}

    if method == "initialize":
        if UPSTREAM_MCP_WSS:
            return None
        return _jsonrpc_ok(
            request_id,
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {"tools": {}},
                "serverInfo": {"name": APP_NAME, "version": "1.0.0"},
            },
        )

    if method == "tools/list":
        if UPSTREAM_MCP_WSS:
            return None
        return _jsonrpc_ok(request_id, {"tools": [_local_tool_schema()], "nextCursor": ""})

    if method == "tools/call":
        name = (params.get("name") if isinstance(params, dict) else "") or ""
        arguments = (params.get("arguments") if isinstance(params, dict) else {}) or {}
        if name == LOCAL_TOOL_NAME:
            song_name = ""
            if isinstance(arguments, dict):
                song_name = str(arguments.get("song_name") or "")
            return _make_tools_call_success(request_id, search_youtube_music(song_name))

        if REQUIRE_UPSTREAM and not UPSTREAM_MCP_WSS:
            return _jsonrpc_error(request_id, -32601, f"Unknown tool: {name}")

    return None


@app.websocket("/mcp/")
async def mcp_ws_bridge(websocket: WebSocket):
    token = websocket.query_params.get("token", "")
    if not MCP_AUTH_TOKEN or token != MCP_AUTH_TOKEN:
        await websocket.close(code=1008, reason="Unauthorized")
        return

    await websocket.accept()

    upstream = None
    pending_tools_list_ids: Set[Any] = set()

    if UPSTREAM_MCP_WSS:
        try:
            upstream = await websockets.connect(UPSTREAM_MCP_WSS)
        except Exception:
            upstream = None

    async def upstream_to_client_loop():
        if upstream is None:
            return
        try:
            async for message in upstream:
                out_text = str(message)
                try:
                    obj = json.loads(out_text)
                    response_id = obj.get("id")
                    if response_id in pending_tools_list_ids and isinstance(obj.get("result"), dict):
                        obj["result"] = _merge_tools_list_result(obj["result"])
                        pending_tools_list_ids.discard(response_id)
                        out_text = json.dumps(obj, ensure_ascii=False)
                except Exception:
                    pass
                await websocket.send_text(out_text)
        except Exception:
            pass

    upstream_task = None
    if upstream is not None:
        import asyncio

        upstream_task = asyncio.create_task(upstream_to_client_loop())

    try:
        while True:
            text = await websocket.receive_text()

            try:
                payload = json.loads(text)
            except Exception:
                if upstream is not None:
                    await upstream.send(text)
                continue

            local_response = _handle_local_request(payload)
            if local_response is not None:
                await websocket.send_text(json.dumps(local_response, ensure_ascii=False))
                continue

            method = payload.get("method")
            if method == "tools/list" and payload.get("id") is not None:
                pending_tools_list_ids.add(payload.get("id"))

            if upstream is not None:
                await upstream.send(text)
            else:
                req_id = payload.get("id")
                if req_id is not None:
                    err = _jsonrpc_error(req_id, -32000, "Upstream MCP is not connected")
                    await websocket.send_text(json.dumps(err, ensure_ascii=False))

    except WebSocketDisconnect:
        pass
    finally:
        if upstream_task is not None:
            upstream_task.cancel()
        if upstream is not None:
            try:
                await upstream.close()
            except Exception:
                pass


# Mount MCP app at /sse
try:
    app.mount("/sse", _pick_mcp_asgi_app())
except Exception as e:
    # Fail fast when endpoint is called
    @app.get("/sse")
    def mcp_boot_error():
        raise HTTPException(status_code=500, detail=f"MCP bootstrap error: {e}")


if __name__ == "__main__":
    import uvicorn

    host = os.getenv("HOST", "0.0.0.0")
    port = int(os.getenv("PORT", "8765"))
    uvicorn.run("app:app", host=host, port=port, reload=False)
