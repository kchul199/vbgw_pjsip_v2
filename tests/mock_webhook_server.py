import http.server
import json

class WebhookHandler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        content_length = int(self.headers['Content-Length'])
        post_data = self.rfile.read(content_length)
        
        print("\n--- Received Webhook ---")
        print(f"Path: {self.path}")
        print(f"Headers: \n{self.headers}")
        
        try:
            payload = json.loads(post_data.decode('utf-8'))
            print(f"Payload: \n{json.dumps(payload, indent=2, ensure_ascii=False)}")
        except Exception as e:
            print(f"Raw Body: {post_data.decode('utf-8')}")
            print(f"Error parsing JSON: {e}")

        self.send_response(200)
        self.send_header('Content-type', 'application/json')
        self.end_headers()
        self.wfile.write(json.dumps({"status": "ok"}).encode('utf-8'))

if __name__ == '__main__':
    port = 8888
    print(f"Starting Mock Webhook Server on port {port}...")
    http.server.HTTPServer(('0.0.0.0', port), WebhookHandler).serve_forever()
