<template>
  <div>
    <div class="upload-box">
      <div class="upload-header">
        <h3>智能上传引擎</h3>
        <p>支持拖拽上传、断点续传、分片上传、批量上传、粘贴上传</p>
      </div>
      <div class="dropzone" :class="{ drag }" @click="$refs.fileInput.click()" @dragover.prevent="drag = true" @dragleave="drag = false" @drop.prevent="handleDrop">
        <div class="drop-inner">
          <div class="drop-icon"><i class="fas fa-cloud-upload-alt"></i></div>
          <div>
            <h2>拖拽文件上传</h2>
            <p>支持图片、视频、PDF、ZIP、音频等 26 种文件格式</p>
            <div class="upload-actions">
              <button class="btn-primary" @click.stop="$refs.fileInput.click()">选择文件</button>
              <button class="btn-secondary" @click.stop>粘贴截图</button>
            </div>
          </div>
        </div>
        <input type="file" ref="fileInput" style="display:none" @change="handleFiles" multiple accept="image/*,video/*,audio/*,.pdf,.zip,.rar,.7z,.doc,.docx,.xls,.xlsx,.ppt,.pptx,.txt,.md,.csv,.json,.xml,.html,.css,.js" />
      </div>

      <!-- Upload Queue Panel -->
      <div class="queue" v-if="uploadStore.queue.length > 0">
        <div class="queue-header">
          <div class="queue-header-left">
            <h3>上传队列</h3>
            <span class="queue-stats">
              <span class="stat-item total">共 {{ uploadStore.totalCount }} 个</span>
              <span class="stat-item pending" v-if="uploadStore.pendingCount > 0">{{ uploadStore.pendingCount }} 等待</span>
              <span class="stat-item uploading" v-if="uploadStore.uploadingCount > 0">{{ uploadStore.uploadingCount }} 上传中</span>
              <span class="stat-item paused" v-if="uploadStore.pausedCount > 0">{{ uploadStore.pausedCount }} 暂停</span>
              <span class="stat-item completed" v-if="uploadStore.completedCount > 0">{{ uploadStore.completedCount }} 完成</span>
              <span class="stat-item failed" v-if="uploadStore.failedCount > 0">{{ uploadStore.failedCount }} 失败</span>
            </span>
          </div>
          <div class="queue-header-right">
            <span class="total-speed" v-if="uploadStore.totalSpeed > 0">
              <i class="fas fa-bolt"></i> {{ uploadStore.formatSpeed(uploadStore.totalSpeed) }}
            </span>
            <button class="batch-btn" @click="uploadStore.pauseAll()" v-if="uploadStore.uploadingCount > 0">
              <i class="fas fa-pause"></i> 全部暂停
            </button>
            <button class="batch-btn" @click="uploadStore.resumeAll()" v-if="uploadStore.pausedCount > 0">
              <i class="fas fa-play"></i> 全部恢复
            </button>
            <button class="batch-btn danger" @click="uploadStore.clearCompleted()" v-if="uploadStore.completedCount > 0">
              <i class="fas fa-trash-alt"></i> 清除完成
            </button>
            <button class="batch-btn danger" @click="confirmClearAll()" v-if="uploadStore.queue.length > 0">
              <i class="fas fa-times-circle"></i> 清空队列
            </button>
          </div>
        </div>
        <div class="queue-list">
          <div v-for="item in uploadStore.queue" :key="item.id" class="upload-item" :class="'status-' + item.status">
            <!-- Thumbnail / Icon -->
            <div class="upload-preview">
              <img v-if="item.thumbnail" :src="item.thumbnail" class="thumb-img" />
              <i v-else class="fas fa-file" style="font-size:1.5rem;color:#9ca3af"></i>
            </div>

            <!-- Body -->
            <div class="upload-body">
              <div class="upload-info-row">
                <span class="file-name" :title="item.name">{{ item.name }}</span>
                <span class="file-size">{{ item.formattedSize }}</span>
              </div>

              <!-- Progress Bar -->
              <div class="progress">
                <div class="progress-bar" :class="'bar-' + item.status" :style="{width: item.progress + '%'}"></div>
              </div>

              <!-- Status Row -->
              <div class="upload-status-row">
                <span class="status-text" :class="'text-' + item.status">
                  <template v-if="item.status === 'pending'"><i class="fas fa-clock"></i> 等待中</template>
                  <template v-else-if="item.status === 'uploading'"><i class="fas fa-spinner fa-spin"></i> {{ item.progress }}%</template>
                  <template v-else-if="item.status === 'paused'"><i class="fas fa-pause-circle"></i> 已暂停 {{ item.progress }}%</template>
                  <template v-else-if="item.status === 'completed'"><i class="fas fa-check-circle"></i> 上传完成</template>
                  <template v-else-if="item.status === 'failed'"><i class="fas fa-exclamation-circle"></i> {{ item.error || '上传失败' }}</template>
                </span>
                <span class="speed-eta" v-if="item.status === 'uploading'">
                  {{ item.formattedSpeed }}
                  <span v-if="item.eta" class="eta"> ETA {{ item.eta }}</span>
                </span>
              </div>
            </div>

            <!-- Action Buttons -->
            <div class="upload-actions-col">
              <!-- Pause / Resume -->
              <button v-if="item.status === 'uploading'" class="action-btn" @click.stop="uploadStore.pauseUpload(item.id)" title="暂停">
                <i class="fas fa-pause"></i>
              </button>
              <button v-else-if="item.status === 'paused'" class="action-btn primary" @click.stop="uploadStore.resumeUpload(item.id)" title="恢复">
                <i class="fas fa-play"></i>
              </button>
              <!-- Retry -->
              <button v-else-if="item.status === 'failed'" class="action-btn warning" @click.stop="uploadStore.retryUpload(item.id)" title="重试">
                <i class="fas fa-redo"></i>
              </button>
              <!-- Cancel -->
              <button v-if="item.status !== 'completed'" class="action-btn danger" @click.stop="uploadStore.cancelUpload(item.id)" title="取消">
                <i class="fas fa-times"></i>
              </button>
              <!-- Copy URL -->
              <button v-if="item.status === 'completed' && item.url" class="action-btn primary" @click.stop="copyUrl(item.url)" title="复制链接">
                <i class="fas fa-link"></i>
              </button>
            </div>
          </div>
        </div>
      </div>

      <!-- Upload History -->
      <div class="history-section" v-if="uploadStore.uploadHistory.length > 0">
        <div class="history-header">
          <h3><i class="fas fa-history"></i> 上传历史</h3>
          <button class="batch-btn danger" @click="uploadStore.clearHistory()">
            <i class="fas fa-trash-alt"></i> 清除历史
          </button>
        </div>
        <div class="history-list">
          <div v-for="(item, index) in uploadStore.uploadHistory.slice(0, 20)" :key="index" class="history-item">
            <div class="history-icon"><i class="fas fa-check-circle"></i></div>
            <div class="history-body">
              <span class="history-name">{{ item.name }}</span>
              <span class="history-meta">{{ item.formattedSize }} &middot; {{ uploadStore.formatHistoryTime(item.time) }}</span>
            </div>
            <button v-if="item.url" class="action-btn primary" @click.stop="copyUrl(item.url)" title="复制链接">
              <i class="fas fa-link"></i>
            </button>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue'
import { useUploadStore } from '../stores/upload'

const uploadStore = useUploadStore()
const drag = ref(false)
const fileInput = ref(null)

function handleFiles(e) {
  uploadStore.addFiles(e.target.files)
  uploadStore.startUpload()
}

function handleDrop(e) {
  drag.value = false
  uploadStore.addFiles(e.dataTransfer.files)
  uploadStore.startUpload()
}

function confirmClearAll() {
  if (confirm('确定清空所有上传队列？')) {
    uploadStore.clearAll()
  }
}

async function copyUrl(url) {
  try {
    await navigator.clipboard.writeText(url)
    // Simple visual feedback
    const el = event.target.closest('.action-btn')
    if (el) {
      el.classList.add('copied')
      setTimeout(() => el.classList.remove('copied'), 1200)
    }
  } catch {
    // Fallback
    const ta = document.createElement('textarea')
    ta.value = url
    document.body.appendChild(ta)
    ta.select()
    document.execCommand('copy')
    document.body.removeChild(ta)
  }
}
</script>

<style scoped>
/* === Box === */
.upload-box {
  background: #111111;
  border-radius: 24px;
  overflow: hidden;
  border: 1px solid #1e1e1e;
}

.upload-header {
  padding: 28px;
  border-bottom: 1px solid #1e1e1e;
}

.upload-header h3 {
  font-size: 22px;
  font-weight: 800;
  color: #ffffff;
}

.upload-header p {
  margin-top: 8px;
  color: #666;
  font-size: 13px;
}

/* === Dropzone === */
.dropzone {
  margin: 20px;
  padding: 48px 36px;
  border-radius: 20px;
  border: 2px dashed #2a2a2a;
  background: #0a0a0a;
  transition: all 0.2s;
  cursor: pointer;
}

.dropzone:hover,
.dropzone.drag {
  border-color: #ffffff;
  background: #141414;
}

.drop-inner {
  display: flex;
  justify-content: center;
  align-items: center;
  gap: 30px;
  text-align: center;
  flex-direction: column;
}

.drop-icon {
  width: 80px;
  height: 80px;
  border-radius: 20px;
  background: #ffffff;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #000000;
  font-size: 2rem;
  flex-shrink: 0;
}

.drop-inner h2 {
  font-size: 26px;
  font-weight: 800;
  color: #ffffff;
}

.drop-inner p {
  margin-top: 10px;
  color: #666;
  line-height: 1.8;
  font-size: 13px;
}

.upload-actions {
  margin-top: 24px;
  display: flex;
  gap: 12px;
  justify-content: center;
}

.btn-primary {
  height: 46px;
  padding: 0 24px;
  border: none;
  border-radius: 12px;
  background: #ffffff;
  color: #000000;
  font-weight: 700;
  cursor: pointer;
  font-size: 14px;
  transition: all 0.15s;
}

.btn-primary:hover {
  transform: translateY(-1px);
  box-shadow: 0 8px 24px rgba(255, 255, 255, 0.15);
}

.btn-secondary {
  height: 46px;
  padding: 0 24px;
  border: 1px solid #333;
  border-radius: 12px;
  background: transparent;
  color: #999;
  font-weight: 600;
  cursor: pointer;
  font-size: 14px;
  transition: all 0.15s;
}

.btn-secondary:hover {
  border-color: #666;
  color: #fff;
}

/* === Queue Panel === */
.queue {
  margin: 0 20px 20px;
  border-radius: 16px;
  overflow: hidden;
  border: 1px solid #1e1e1e;
}

.queue-header {
  padding: 16px 20px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  background: #0a0a0a;
  border-bottom: 1px solid #1e1e1e;
  flex-wrap: wrap;
  gap: 12px;
}

.queue-header-left {
  display: flex;
  align-items: center;
  gap: 16px;
  flex-wrap: wrap;
}

.queue-header h3 {
  font-size: 14px;
  font-weight: 700;
  color: #ffffff;
  white-space: nowrap;
}

.queue-stats {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}

.stat-item {
  font-size: 11px;
  padding: 3px 8px;
  border-radius: 6px;
  font-weight: 600;
  white-space: nowrap;
}

.stat-item.total { background: #1a1a1a; color: #888; }
.stat-item.pending { background: #1a1a2e; color: #6b7280; }
.stat-item.uploading { background: #0f172a; color: #3b82f6; }
.stat-item.paused { background: #1a1a1a; color: #eab308; }
.stat-item.completed { background: #052e16; color: #22c55e; }
.stat-item.failed { background: #1c0b0b; color: #ef4444; }

.queue-header-right {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.total-speed {
  font-size: 12px;
  color: #3b82f6;
  font-weight: 700;
  white-space: nowrap;
}

.total-speed i {
  margin-right: 4px;
}

.batch-btn {
  height: 32px;
  padding: 0 12px;
  border: 1px solid #2a2a2a;
  border-radius: 8px;
  background: #111;
  color: #999;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  transition: all 0.15s;
  white-space: nowrap;
}

.batch-btn:hover {
  border-color: #555;
  color: #fff;
}

.batch-btn.danger {
  border-color: #2a1515;
  color: #ef4444;
}

.batch-btn.danger:hover {
  border-color: #ef4444;
  background: #1c0b0b;
}

/* === Queue List === */
.queue-list {
  background: #111;
  max-height: 420px;
  overflow: auto;
}

.queue-list::-webkit-scrollbar {
  width: 4px;
}

.queue-list::-webkit-scrollbar-track {
  background: transparent;
}

.queue-list::-webkit-scrollbar-thumb {
  background: #333;
  border-radius: 4px;
}

.upload-item {
  display: flex;
  gap: 14px;
  padding: 14px 18px;
  border-bottom: 1px solid #1a1a1a;
  transition: background 0.15s;
  align-items: flex-start;
}

.upload-item:hover {
  background: #161616;
}

.upload-item:last-child {
  border-bottom: none;
}

/* === Preview === */
.upload-preview {
  width: 52px;
  height: 52px;
  border-radius: 12px;
  background: #1a1a1a;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  overflow: hidden;
}

.thumb-img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  border-radius: 12px;
}

/* === Body === */
.upload-body {
  flex: 1;
  min-width: 0;
}

.upload-info-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
}

.file-name {
  font-weight: 700;
  font-size: 13px;
  color: #e5e5e5;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  flex: 1;
  min-width: 0;
}

.file-size {
  font-size: 12px;
  color: #666;
  white-space: nowrap;
  flex-shrink: 0;
}

.progress {
  margin-top: 8px;
  height: 6px;
  background: #1e1e1e;
  border-radius: 99px;
  overflow: hidden;
}

.progress-bar {
  height: 100%;
  border-radius: 99px;
  transition: width 0.3s ease;
}

.bar-pending { background: #333; }
.bar-uploading { background: #3b82f6; }
.bar-paused { background: #eab308; }
.bar-completed { background: #22c55e; }
.bar-failed { background: #ef4444; }

.upload-status-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-top: 6px;
}

.status-text {
  font-size: 11px;
  font-weight: 600;
  display: flex;
  align-items: center;
  gap: 4px;
}

.text-pending { color: #666; }
.text-uploading { color: #3b82f6; }
.text-paused { color: #eab308; }
.text-completed { color: #22c55e; }
.text-failed { color: #ef4444; }

.speed-eta {
  font-size: 11px;
  color: #888;
  font-weight: 600;
  white-space: nowrap;
}

.eta {
  color: #555;
  margin-left: 6px;
}

/* === Action Buttons === */
.upload-actions-col {
  display: flex;
  gap: 6px;
  flex-shrink: 0;
  align-items: flex-start;
  padding-top: 4px;
}

.action-btn {
  width: 32px;
  height: 32px;
  border-radius: 8px;
  border: 1px solid #2a2a2a;
  background: #1a1a1a;
  color: #888;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 12px;
  transition: all 0.15s;
}

.action-btn:hover {
  border-color: #555;
  color: #fff;
}

.action-btn.primary {
  border-color: #1e3a5f;
  color: #3b82f6;
}

.action-btn.primary:hover {
  background: #0f172a;
  border-color: #3b82f6;
}

.action-btn.primary.copied {
  border-color: #22c55e;
  color: #22c55e;
}

.action-btn.warning {
  border-color: #3d2e00;
  color: #eab308;
}

.action-btn.warning:hover {
  background: #1a1500;
  border-color: #eab308;
}

.action-btn.danger {
  border-color: #3b1111;
  color: #ef4444;
}

.action-btn.danger:hover {
  background: #1c0b0b;
  border-color: #ef4444;
}

/* === History === */
.history-section {
  margin: 0 20px 20px;
  border-radius: 16px;
  overflow: hidden;
  border: 1px solid #1e1e1e;
}

.history-header {
  padding: 14px 20px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  background: #0a0a0a;
  border-bottom: 1px solid #1e1e1e;
}

.history-header h3 {
  font-size: 14px;
  font-weight: 700;
  color: #ffffff;
  display: flex;
  align-items: center;
  gap: 8px;
}

.history-list {
  background: #111;
  max-height: 280px;
  overflow: auto;
}

.history-list::-webkit-scrollbar {
  width: 4px;
}

.history-list::-webkit-scrollbar-thumb {
  background: #333;
  border-radius: 4px;
}

.history-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 18px;
  border-bottom: 1px solid #1a1a1a;
  transition: background 0.15s;
}

.history-item:hover {
  background: #161616;
}

.history-item:last-child {
  border-bottom: none;
}

.history-icon {
  color: #22c55e;
  font-size: 14px;
  flex-shrink: 0;
}

.history-body {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.history-name {
  font-size: 13px;
  font-weight: 600;
  color: #ccc;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.history-meta {
  font-size: 11px;
  color: #555;
}
</style>
