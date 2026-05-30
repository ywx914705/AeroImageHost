<template>
  <Teleport to="body">
    <div v-if="visible" class="drawer-overlay" @click.self="$emit('close')">
      <div class="drawer" :class="{ open: visible }">
        <div class="drawer-header">
          <h2>文件详情</h2>
          <button class="drawer-close" @click="$emit('close')"><i class="fas fa-times"></i></button>
        </div>
        <div class="drawer-body" v-if="file">
          <!-- 预览 -->
          <div class="drawer-preview" v-if="isImage">
            <img :src="`/api/i/${file.file_id}?token=${token}`" />
          </div>
          <div class="drawer-preview" v-else>
            <div class="preview-icon" :style="{background: getFileColor(file)+'18', color: getFileColor(file)}">
              <i :class="getFileIcon(file)" style="font-size:3rem"></i>
            </div>
          </div>

          <!-- 元信息 -->
          <div class="drawer-info">
            <div class="info-box">
              <div class="info-label">文件名称</div>
              <div class="info-value">{{ file.filename }}</div>
            </div>
            <div class="info-box">
              <div class="info-label">文件大小</div>
              <div class="info-value">{{ formatSize(file.size) }}</div>
            </div>
            <div class="info-box">
              <div class="info-label">文件类型</div>
              <div class="info-value">{{ file.mime_type }}</div>
            </div>
            <div class="info-box" v-if="file.width">
              <div class="info-label">图片尺寸</div>
              <div class="info-value">{{ file.width }} × {{ file.height }}</div>
            </div>
            <div class="info-box">
              <div class="info-label">上传时间</div>
              <div class="info-value">{{ formatDate(file.upload_time) }}</div>
            </div>
            <div class="info-box">
              <div class="info-label">状态</div>
              <div class="info-value">
                <span class="status-badge" :class="file.is_public ? 'public' : 'private'">
                  {{ file.is_public ? '公开' : '私有' }}
                </span>
              </div>
            </div>
          </div>

          <!-- 分享链接 -->
          <div class="drawer-section">
            <div class="section-title">分享链接</div>
            <div class="share-row">
              <input :value="shareUrl" readonly />
              <button class="copy-btn" @click="copyText(shareUrl)">复制</button>
            </div>
            <div class="share-row">
              <input :value="markdownCode" readonly />
              <button class="copy-btn" @click="copyText(markdownCode)">Markdown</button>
            </div>
          </div>

          <!-- 操作按钮 -->
          <div class="drawer-actions">
            <button class="action-btn primary" @click="downloadFile"><i class="fas fa-download"></i> 下载</button>
            <button class="action-btn" @click="togglePublic"><i :class="file.is_public ? 'fas fa-globe' : 'fas fa-lock'"></i> {{ file.is_public ? '设为私有' : '设为公开' }}</button>
            <button class="action-btn danger" @click="deleteFile"><i class="fas fa-trash"></i> 删除</button>
          </div>
        </div>
      </div>
    </div>
  </Teleport>
</template>

<script setup>
import { computed } from 'vue'
import { ElMessage } from 'element-plus'
import api from '../utils/api'

const props = defineProps({
  visible: Boolean,
  file: Object
})

const emit = defineEmits(['close', 'refresh'])

const token = localStorage.getItem('token') || ''

const isImage = computed(() => props.file?.mime_type?.startsWith('image/'))
const shareUrl = computed(() => `${window.location.origin}/api/i/${props.file?.file_id}?token=${token}`)
const markdownCode = computed(() => `![${props.file?.filename}](${shareUrl.value})`)

function formatSize(b) { if(!b)return '0';if(b<1024)return b+'B';if(b<1048576)return(b/1024).toFixed(1)+'KB';if(b<1073741824)return(b/1048576).toFixed(1)+'MB';return(b/1073741824).toFixed(2)+'GB' }
function formatDate(t) { return new Date(t*1000).toLocaleString('zh-CN') }
function getFileColor(f) { const ext=f.filename?.split('.').pop()?.toLowerCase();const m={jpg:'#2563eb',png:'#2563eb',pdf:'#ef4444',doc:'#0ea5e9',xls:'#10b981',ppt:'#f97316',mp4:'#8b5cf6',mp3:'#ec4899'};return m[ext]||'#64748b' }
function getFileIcon(f) { const m=f.mime_type||'';if(m.includes('pdf'))return'fas fa-file-pdf';if(m.includes('word'))return'fas fa-file-word';if(m.startsWith('video/'))return'fas fa-file-video';if(m.startsWith('audio/'))return'fas fa-file-audio';return'fas fa-file' }

async function copyText(text) { await navigator.clipboard.writeText(text); ElMessage.success('已复制') }
function downloadFile() { window.open(`/api/i/${props.file.file_id}?download=1&token=${token}`) }
async function togglePublic() { await api.put(`/file/${props.file.file_id}/public`); props.file.is_public = !props.file.is_public; emit('refresh'); ElMessage.success(props.file.is_public ? '已公开' : '已设为私有') }
async function deleteFile() { if(confirm('确定删除？')) { await api.delete(`/file/${props.file.file_id}`); emit('close'); emit('refresh'); ElMessage.success('已删除') } }
</script>

<style scoped>
.drawer-overlay { position: fixed; inset: 0; background: rgba(0,0,0,.3); z-index: 9998; }
.drawer { position: fixed; right: 0; top: 0; width: 420px; height: 100vh; background: white; z-index: 9999; box-shadow: -20px 0 60px rgba(0,0,0,.08); padding: 0; overflow-y: auto; transition: transform .3s cubic-bezier(.4,0,.2,1); }
.drawer-header { display: flex; justify-content: space-between; align-items: center; padding: 24px 28px; border-bottom: 1px solid #f1f5f9; }
.drawer-header h2 { font-size: 20px; font-weight: 800; }
.drawer-close { width: 36px; height: 36px; border-radius: 10px; border: none; background: #f3f4f6; cursor: pointer; display: flex; align-items: center; justify-content: center; color: #6b7280; }
.drawer-close:hover { background: #e5e7eb; }
.drawer-body { padding: 24px 28px; }
.drawer-preview { border-radius: 16px; overflow: hidden; margin-bottom: 20px; background: #f3f4f6; }
.drawer-preview img { width: 100%; display: block; }
.preview-icon { padding: 48px; display: flex; align-items: center; justify-content: center; }
.drawer-info { display: flex; flex-direction: column; gap: 12px; margin-bottom: 24px; }
.info-box { padding: 14px 16px; border-radius: 12px; background: #f7f9fc; }
.info-label { font-size: 11px; color: #6b7280; font-weight: 600; text-transform: uppercase; letter-spacing: 0.3px; }
.info-value { margin-top: 6px; font-size: 14px; font-weight: 600; color: #111827; word-break: break-all; }
.status-badge { padding: 3px 10px; border-radius: 6px; font-size: 12px; font-weight: 600; }
.status-badge.public { background: #ecfdf5; color: #10b981; }
.status-badge.private { background: #f3f4f6; color: #6b7280; }
.drawer-section { margin-bottom: 24px; }
.section-title { font-size: 12px; font-weight: 700; color: #9ca3af; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 10px; }
.share-row { display: flex; gap: 8px; margin-bottom: 8px; }
.share-row input { flex: 1; height: 42px; border: 1px solid #e5e7eb; border-radius: 10px; padding: 0 12px; font-size: 13px; background: #f7f9fc; color: #111827; }
.copy-btn { width: 80px; border: none; border-radius: 10px; background: #111827; color: white; font-weight: 600; font-size: 12px; cursor: pointer; }
.copy-btn:hover { background: #374151; }
.drawer-actions { display: flex; gap: 10px; margin-top: 24px; }
.action-btn { flex: 1; height: 44px; border: none; border-radius: 12px; background: #f3f4f6; font-weight: 600; font-size: 13px; cursor: pointer; display: flex; align-items: center; justify-content: center; gap: 6px; }
.action-btn:hover { background: #e5e7eb; }
.action-btn.primary { background: #2563eb; color: white; }
.action-btn.primary:hover { background: #1d4ed8; }
.action-btn.danger { color: #ef4444; }
.action-btn.danger:hover { background: #fef2f2; }
</style>
