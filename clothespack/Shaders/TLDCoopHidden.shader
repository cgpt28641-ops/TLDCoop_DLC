// TLDCoop_DLC: draws nothing. A body's own jacket or sweater (a submesh of its own) gets this under a friend's coat.
Shader "TLDCoop/Hidden" {
    SubShader {
        Tags { "Queue" = "Geometry" "RenderType" = "Opaque" }
        Pass {
            ColorMask 0
            ZWrite Off
            ZTest Never
        }
    }
}
