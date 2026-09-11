import json
import os
from typing import Any, Dict, List

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse
from dotenv import load_dotenv
from yt_dlp import YoutubeDL

# FastMCP import compatibility
try:
    from fastmcp import FastMCP  # type: ignore
except Exception:  # pragma: no cover
    from mcp.server.fastmcp import FastMCP  # type: ignore


APP_NAME = "mcp-youtube-server"
load_dotenv()
MCP_AUTH_TOKEN = os.getenv("MCP_AUTH_TOKEN", "")
YT_MAX_RESULTS = max(1, int(os.getenv("YT_MAX_RESULTS", "5")))

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
    if request.url.path.startswith("/sse"):
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
    return {"ok": True, "service": APP_NAME}


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
